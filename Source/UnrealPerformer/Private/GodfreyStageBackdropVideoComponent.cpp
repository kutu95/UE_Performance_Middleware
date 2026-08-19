#include "GodfreyStageBackdropVideoComponent.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GodfreyPerformanceLog.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "MediaPlateComponent.h"
#include "MediaPlayer.h"
#include "MediaTexture.h"
#include "Misc/Paths.h"
#include "UnrealPerformerGodfreySettings.h"

namespace
{
AActor* FindActorByLabel(UWorld* World, const TCHAR* Label)
{
	if (!World || !Label)
	{
		return nullptr;
	}
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->GetActorNameOrLabel().Equals(Label, ESearchCase::IgnoreCase))
		{
			return *It;
		}
	}
	return nullptr;
}

bool IsMediaPlateActor(const AActor* Actor)
{
	if (!Actor)
	{
		return false;
	}
	if (Actor->FindComponentByClass<UMediaPlateComponent>())
	{
		return true;
	}
	const FString Label = Actor->GetActorNameOrLabel();
	return Label.StartsWith(TEXT("MediaPlate"), ESearchCase::IgnoreCase)
		|| Actor->GetClass()->GetName().Contains(TEXT("MediaPlate"));
}

AActor* FindBackdropMediaPlate(UWorld* World)
{
	AActor* Fallback = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* const Actor = *It;
		if (!IsMediaPlateActor(Actor))
		{
			continue;
		}
		if (Actor->GetActorNameOrLabel().Equals(TEXT("MediaPlate2"), ESearchCase::IgnoreCase))
		{
			return Actor;
		}
		if (!Fallback)
		{
			Fallback = Actor;
		}
	}
	return Fallback;
}
} // namespace

UGodfreyStageBackdropVideoComponent::UGodfreyStageBackdropVideoComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UGodfreyStageBackdropVideoComponent::BeginPlay()
{
	Super::BeginPlay();
	if (GetDefault<UUnrealPerformerGodfreySettings>()->bGodfreyEnableStageBackdropVideo)
	{
		StartBackdropVideo();
	}
}

void UGodfreyStageBackdropVideoComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopBackdropVideo();
	Super::EndPlay(EndPlayReason);
}

void UGodfreyStageBackdropVideoComponent::StartBackdropVideo()
{
	UWorld* const World = GetWorld();
	if (!World)
	{
		return;
	}

	const UUnrealPerformerGodfreySettings* const Settings = GetDefault<UUnrealPerformerGodfreySettings>();
	FString Rel = Settings->GodfreyStageBackdropVideoContentPath.TrimStartAndEnd();
	Rel.ReplaceInline(TEXT("\\"), TEXT("/"));
	while (Rel.StartsWith(TEXT("/")))
	{
		Rel.RightChopInline(1);
	}
	const FString FullPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / Rel);
	if (!FPaths::FileExists(FullPath))
	{
		UE_LOG(LogGodfreyPerformance, Warning,
			TEXT("StageBackdropVideo: file missing '%s'."), *FullPath);
		return;
	}

	if (TryPlayOnMediaPlate(FullPath))
	{
		HideExhibitFloorInPlay();
		return;
	}

	AActor* Backdrop = FindActorByLabel(World, TEXT("Stage_Backdrop"));
	if (!Backdrop)
	{
		UE_LOG(LogGodfreyPerformance, Warning,
			TEXT("StageBackdropVideo: MediaPlate2 and Stage_Backdrop not found."));
		return;
	}

	UStaticMeshComponent* Mesh = Backdrop->FindComponentByClass<UStaticMeshComponent>();
	if (!Mesh)
	{
		UE_LOG(LogGodfreyPerformance, Warning, TEXT("StageBackdropVideo: Stage_Backdrop has no StaticMeshComponent."));
		return;
	}

	if (!MediaPlayer)
	{
		MediaPlayer = NewObject<UMediaPlayer>(this, TEXT("StageBackdropPlayer"));
		MediaPlayer->PlayOnOpen = true;
		MediaPlayer->OnMediaOpened.AddDynamic(this, &UGodfreyStageBackdropVideoComponent::HandleMediaOpened);
		MediaPlayer->OnMediaOpenFailed.AddDynamic(this, &UGodfreyStageBackdropVideoComponent::HandleMediaOpenFailed);
	}
	if (!MediaTexture)
	{
		MediaTexture = NewObject<UMediaTexture>(this, TEXT("StageBackdropTexture"));
		MediaTexture->NewStyleOutput = false;
		MediaTexture->AutoClear = true;
		MediaTexture->EnableGenMips = false;
		MediaTexture->AddressX = TA_Clamp;
		MediaTexture->AddressY = TA_Clamp;
		MediaTexture->SetMediaPlayer(MediaPlayer);
		MediaTexture->UpdateResource();
	}

	ApplyTextureToBackdrop(Mesh);
	HideExtraMediaPlateActors(nullptr);
	HideExhibitFloorInPlay();
	Backdrop->SetActorHiddenInGame(false);

	MediaPlayer->SetLooping(true);
	const bool bOpened = MediaPlayer->OpenFile(FullPath);
	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("StageBackdropVideo: OpenFile on Stage_Backdrop '%s' ok=%d."), *FullPath, bOpened ? 1 : 0);
	if (bOpened)
	{
		MediaPlayer->Play();
	}
}

void UGodfreyStageBackdropVideoComponent::SizeMediaPlateToFillPortraitHeight(AActor* Plate)
{
	if (!Plate)
	{
		return;
	}

	UWorld* const World = GetWorld();
	AActor* const CameraActor = FindActorByLabel(World, TEXT("Exhibit_CineCamera"));
	AActor* const Backdrop = FindActorByLabel(World, TEXT("Stage_Backdrop"));
	if (!CameraActor)
	{
		UE_LOG(LogGodfreyPerformance, Warning,
			TEXT("StageBackdropVideo: Exhibit_CineCamera missing — cannot size harbour plate to portrait height."));
		return;
	}

	if (UMediaPlateComponent* const PlateComp = Plate->FindComponentByClass<UMediaPlateComponent>())
	{
		PlateComp->SetIsAspectRatioAuto(false);
		PlateComp->SetLetterboxAspectRatio(0.f);
	}

	if (UStaticMeshComponent* const Mesh = Plate->FindComponentByClass<UStaticMeshComponent>())
	{
		Mesh->SetRelativeScale3D(FVector(1.f, 1.f, 1.f));
	}

	// Match Scripts/setup_exhibit_portrait_view.py (15x24 mm filmback, 35 mm lens).
	constexpr float SensorHeightMM = 24.f;
	constexpr float FocalLengthMM = 35.f;
	constexpr float VideoAspect = 16.f / 9.f;
	constexpr float MeshUU = 100.f;
	constexpr float HeightOverscan = 1.01f;

	const FVector CamLoc = CameraActor->GetActorLocation();
	const FVector PlateLoc = Backdrop ? Backdrop->GetActorLocation() : Plate->GetActorLocation();
	float Dist = FVector::DotProduct(PlateLoc - CamLoc, CameraActor->GetActorForwardVector());
	if (Dist < 50.f)
	{
		Dist = FVector::Dist(PlateLoc, CamLoc);
	}
	Dist = FMath::Max(Dist, 50.f);

	const float VerticalFov = 2.f * FMath::Atan((SensorHeightMM * 0.5f) / FocalLengthMM);
	const float FrustumH = 2.f * Dist * FMath::Tan(VerticalFov * 0.5f);
	const float PlateH = FrustumH * HeightOverscan;
	const float PlateW = PlateH * VideoAspect;

	Plate->SetActorScale3D(FVector(1.f, PlateW / MeshUU, PlateH / MeshUU));
	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("StageBackdropVideo: fill-height 16:9 plate dist=%.0f size=(%.0fx%.0f)."),
		Dist, PlateW, PlateH);
}

bool UGodfreyStageBackdropVideoComponent::TryPlayOnMediaPlate(const FString& FullPath)
{
	UWorld* const World = GetWorld();
	AActor* const Plate = FindBackdropMediaPlate(World);
	if (!Plate)
	{
		return false;
	}

	UMediaPlateComponent* const PlateComp = Plate->FindComponentByClass<UMediaPlateComponent>();
	if (!PlateComp)
	{
		UE_LOG(LogGodfreyPerformance, Warning,
			TEXT("StageBackdropVideo: '%s' has no MediaPlateComponent."), *Plate->GetActorNameOrLabel());
		return false;
	}

	Plate->SetActorHiddenInGame(false);
	Plate->SetActorEnableCollision(true);
	if (UStaticMeshComponent* Mesh = Plate->FindComponentByClass<UStaticMeshComponent>())
	{
		Mesh->SetHiddenInGame(false);
		Mesh->SetVisibility(true, true);
	}

	SizeMediaPlateToFillPortraitHeight(Plate);

	PlateComp->bPlayOnOpen = true;
	PlateComp->bAutoPlay = true;
	PlateComp->SetEnableAudio(false);
	PlateComp->SetLoop(true);
	PlateComp->SelectExternalMedia(FullPath);
	PlateComp->Open();
	PlateComp->Play();
	if (UMediaPlayer* const Player = PlateComp->GetMediaPlayer())
	{
		Player->SetLooping(true);
		Player->Play();
	}

	HideExtraMediaPlateActors(Plate);
	HideActorByLabel(TEXT("Stage_Backdrop"), false);

	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("StageBackdropVideo: playing on '%s' file='%s'."),
		*Plate->GetActorNameOrLabel(), *FullPath);
	return true;
}

void UGodfreyStageBackdropVideoComponent::StopBackdropVideo()
{
	if (MediaPlayer)
	{
		MediaPlayer->OnMediaOpened.RemoveDynamic(this, &UGodfreyStageBackdropVideoComponent::HandleMediaOpened);
		MediaPlayer->OnMediaOpenFailed.RemoveDynamic(this, &UGodfreyStageBackdropVideoComponent::HandleMediaOpenFailed);
		MediaPlayer->Close();
	}
}

void UGodfreyStageBackdropVideoComponent::ApplyTextureToBackdrop(UStaticMeshComponent* Mesh)
{
	if (!Mesh || !MediaTexture)
	{
		return;
	}

	UMaterialInterface* const BaseMat = Mesh->GetMaterial(0);
	UMaterialInstanceDynamic* Mid = Mesh->CreateDynamicMaterialInstance(0, BaseMat);
	if (!Mid)
	{
		UE_LOG(LogGodfreyPerformance, Warning, TEXT("StageBackdropVideo: could not create MID on Stage_Backdrop."));
		return;
	}

	TArray<FMaterialParameterInfo> Infos;
	TArray<FGuid> Ids;
	Mid->GetAllTextureParameterInfo(Infos, Ids);
	if (Infos.Num() == 0 && BaseMat)
	{
		BaseMat->GetAllTextureParameterInfo(Infos, Ids);
	}
	for (const FMaterialParameterInfo& Info : Infos)
	{
		Mid->SetTextureParameterValue(Info.Name, MediaTexture);
	}
	if (Infos.Num() == 0)
	{
		static const FName FallbackNames[] = {
			FName(TEXT("Texture")), FName(TEXT("BaseColor")), FName(TEXT("BaseColorTexture")),
			FName(TEXT("Diffuse")), FName(TEXT("DiffuseMap")), FName(TEXT("Albedo"))
		};
		for (const FName Name : FallbackNames)
		{
			Mid->SetTextureParameterValue(Name, MediaTexture);
		}
	}
	Mesh->SetMaterial(0, Mid);
}

void UGodfreyStageBackdropVideoComponent::HideExtraMediaPlateActors(AActor* KeepVisible)
{
	UWorld* const World = GetWorld();
	if (!World)
	{
		return;
	}
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* const Actor = *It;
		if (!IsMediaPlateActor(Actor) || Actor == KeepVisible)
		{
			continue;
		}
		Actor->SetActorHiddenInGame(true);
		Actor->SetActorEnableCollision(false);
		UE_LOG(LogGodfreyPerformance, Log,
			TEXT("StageBackdropVideo: hid extra plate '%s'."), *Actor->GetActorNameOrLabel());
	}
}

void UGodfreyStageBackdropVideoComponent::HideActorByLabel(const TCHAR* Label, const bool bDisableCollision)
{
	AActor* const Actor = FindActorByLabel(GetWorld(), Label);
	if (!Actor)
	{
		return;
	}
	Actor->SetActorHiddenInGame(true);
	if (bDisableCollision)
	{
		Actor->SetActorEnableCollision(false);
	}
	UE_LOG(LogGodfreyPerformance, Log, TEXT("StageBackdropVideo: hid '%s' in play."), Label);
}

void UGodfreyStageBackdropVideoComponent::HideExhibitFloorInPlay()
{
	if (!GetDefault<UUnrealPerformerGodfreySettings>()->bGodfreyHideExhibitFloorInPlay)
	{
		return;
	}

	UWorld* const World = GetWorld();
	if (!World)
	{
		return;
	}
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* const Actor = *It;
		if (!Actor)
		{
			continue;
		}
		if (Actor->GetActorNameOrLabel().Equals(TEXT("Exhibit_Floor"), ESearchCase::IgnoreCase))
		{
			Actor->SetActorHiddenInGame(true);
			if (UStaticMeshComponent* Mesh = Actor->FindComponentByClass<UStaticMeshComponent>())
			{
				Mesh->SetHiddenInGame(true);
				Mesh->SetCastShadow(false);
				Mesh->SetRenderInMainPass(false);
				Mesh->bUseAsOccluder = false;
				Mesh->bRenderInDepthPass = false;
			}
			UE_LOG(LogGodfreyPerformance, Log, TEXT("StageBackdropVideo: Exhibit_Floor see-through (collision kept)."));
		}
	}
}

void UGodfreyStageBackdropVideoComponent::HandleMediaOpened(FString OpenedUrl)
{
	UE_LOG(LogGodfreyPerformance, Log, TEXT("StageBackdropVideo: opened '%s'."), *OpenedUrl);
	if (MediaPlayer)
	{
		MediaPlayer->SetLooping(true);
		MediaPlayer->Play();
	}
}

void UGodfreyStageBackdropVideoComponent::HandleMediaOpenFailed(FString FailedUrl)
{
	UE_LOG(LogGodfreyPerformance, Warning, TEXT("StageBackdropVideo: open failed '%s'."), *FailedUrl);
}
