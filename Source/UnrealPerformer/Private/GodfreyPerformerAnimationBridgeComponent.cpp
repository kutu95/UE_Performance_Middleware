#include "GodfreyPerformerAnimationBridgeComponent.h"

#include "GodfreyBodyAnimInstance.h"
#include "GodfreyFaceEyeLookAtAnimInstance.h"
#include "GodfreyDiagnostics.h"
#include "GodfreyPerformanceLog.h"
#include "GodfreyPerformanceStateComponent.h"
#include "GodfreyExhibitionQueuePollComponent.h"
#include "GodfreyVisitorPresenceComponent.h"
#include "GodfreyVoiceInputComponent.h"
#include "UnrealPerformerGodfreySettings.h"
#include "Animation/AnimCurveTypes.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/AnimationAsset.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/DataTable.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameModeBase.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMathLibrary.h"
#include "Camera/CameraComponent.h"
#include "LODSyncInterface.h"
#include "GameFramework/Actor.h"
#include "Camera/PlayerCameraManager.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"
#include "HAL/IConsoleManager.h"
#include "InputCoreTypes.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "AnimNodes/AnimNode_CopyPoseFromMesh.h"
#include "ControlRig.h"
#include "MetaHumanComponentBase.h"
#include "MetaHumanComponentUE.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "UObject/Package.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UnrealType.h"
#include "Misc/PackageName.h"

void RegisterGodfreyOperatorCaptureSlateInput();
void UnregisterGodfreyOperatorCaptureSlateInput();

#if WITH_EDITOR
#include "Components/LODSyncComponent.h"
#include "Containers/Ticker.h"
#include "Editor.h"
#include "EditorViewportClient.h"
#include "LevelEditorViewport.h"
#include "Recorder/TakeRecorder.h"
#include "Recorder/TakeRecorderBlueprintLibrary.h"
#include "Recorder/TakeRecorderPanel.h"
#include "Recorder/TakeRecorderParameters.h"
#include "TakeRecorderActorSource.h"
#include "TakeRecorderSources.h"
#include "LevelSequence.h"
#endif

namespace
{
static AActor* ResolveExhibitionCameraActor(UObject* WorldContextObject)
{
	if (!WorldContextObject)
	{
		return nullptr;
	}
	UGodfreyPerformerAnimationBridgeComponent* Bridge =
		Cast<UGodfreyPerformerAnimationBridgeComponent>(WorldContextObject);
	if (Bridge)
	{
		if (IsValid(Bridge->ExplicitFocusTargetActor))
		{
			return Bridge->ExplicitFocusTargetActor.Get();
		}
		if (UWorld* World = Bridge->GetWorld())
		{
			if (!Bridge->ExplicitFocusTargetActorTag.IsNone())
			{
				for (TActorIterator<AActor> It(World); It; ++It)
				{
					AActor* Candidate = *It;
					if (Candidate && Candidate->ActorHasTag(Bridge->ExplicitFocusTargetActorTag))
					{
						Bridge->ExplicitFocusTargetActor = Candidate;
						UE_LOG(LogGodfreyPerformance, Log,
							TEXT("GodfreyPerformerBridge: resolved exhibition camera by tag '%s' actor='%s'."),
							*Bridge->ExplicitFocusTargetActorTag.ToString(),
							*Candidate->GetName());
						return Candidate;
					}
				}
			}
			if (!Bridge->ExplicitFocusTargetActorLabel.IsEmpty())
			{
				for (TActorIterator<AActor> It(World); It; ++It)
				{
					AActor* Candidate = *It;
					if (Candidate && Candidate->GetActorNameOrLabel().Equals(
							Bridge->ExplicitFocusTargetActorLabel, ESearchCase::IgnoreCase))
					{
						Bridge->ExplicitFocusTargetActor = Candidate;
						UE_LOG(LogGodfreyPerformance, Log,
							TEXT("GodfreyPerformerBridge: resolved exhibition camera by label '%s' actor='%s'."),
							*Bridge->ExplicitFocusTargetActorLabel,
							*Candidate->GetName());
						return Candidate;
					}
				}
			}
			if (!Bridge->ExplicitFocusTargetActorName.IsNone())
			{
				for (TActorIterator<AActor> It(World); It; ++It)
				{
					AActor* Candidate = *It;
					if (Candidate && Candidate->GetFName() == Bridge->ExplicitFocusTargetActorName)
					{
						Bridge->ExplicitFocusTargetActor = Candidate;
						UE_LOG(LogGodfreyPerformance, Log,
							TEXT("GodfreyPerformerBridge: resolved exhibition camera by name '%s'."),
							*Bridge->ExplicitFocusTargetActorName.ToString());
						return Candidate;
					}
				}
			}
		}
	}
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(WorldContextObject, 0))
	{
		if (AActor* ViewTarget = PC->GetViewTarget())
		{
			return ViewTarget;
		}
	}
	return nullptr;
}

static AActor* ResolvePrimaryViewTarget(UObject* WorldContextObject)
{
	return ResolveExhibitionCameraActor(WorldContextObject);
}

static bool TryGetExhibitionCameraTransform(UObject* WorldContextObject, FVector& OutLocation, FRotator& OutRotation)
{
	if (const UGodfreyPerformerAnimationBridgeComponent* Bridge =
			Cast<UGodfreyPerformerAnimationBridgeComponent>(WorldContextObject))
	{
		if (Bridge->bPreferPlayerCameraForExhibitionFacing)
		{
			if (APlayerController* PC = UGameplayStatics::GetPlayerController(WorldContextObject, 0))
			{
				if (APlayerCameraManager* PCM = PC->PlayerCameraManager)
				{
					OutLocation = PCM->GetCameraLocation();
					OutRotation = PCM->GetCameraRotation();
					return true;
				}
			}
		}
	}
	if (AActor* CameraActor = ResolveExhibitionCameraActor(WorldContextObject))
	{
		if (const UCameraComponent* CameraComponent = CameraActor->FindComponentByClass<UCameraComponent>())
		{
			OutLocation = CameraComponent->GetComponentLocation();
			OutRotation = CameraComponent->GetComponentRotation();
			return true;
		}
		OutLocation = CameraActor->GetActorLocation();
		OutRotation = CameraActor->GetActorRotation();
		return true;
	}
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(WorldContextObject, 0))
	{
		if (APlayerCameraManager* PCM = PC->PlayerCameraManager)
		{
			OutLocation = PCM->GetCameraLocation();
			OutRotation = PCM->GetCameraRotation();
			return true;
		}
	}
	return false;
}

static bool TryGetPrimaryCameraRotation(UObject* WorldContextObject, FRotator& OutRotation)
{
	FVector Location = FVector::ZeroVector;
	return TryGetExhibitionCameraTransform(WorldContextObject, Location, OutRotation);
}

static bool TryGetPrimaryCameraLocation(UObject* WorldContextObject, FVector& OutLocation)
{
	FRotator Rotation = FRotator::ZeroRotator;
	return TryGetExhibitionCameraTransform(WorldContextObject, OutLocation, Rotation);
}

static bool SnapOwnerYawToExhibitionFacing(UGodfreyPerformerAnimationBridgeComponent* Bridge, const TCHAR* Reason)
{
	if (!Bridge)
	{
		return false;
	}
	AActor* const Owner = Bridge->GetOwner();
	if (!Owner)
	{
		return false;
	}
	float TargetYaw = 0.f;
	if (!Bridge->TryGetExhibitionFacingYaw(TargetYaw))
	{
		return false;
	}
	const FRotator Current = Owner->GetActorRotation();
	if (FMath::Abs(FRotator::NormalizeAxis(Current.Yaw - TargetYaw)) < 0.5f)
	{
		UE_LOG(LogGodfreyPerformance, Log,
			TEXT("GodfreyPerformerBridge: yaw already at exhibition facing (reason=%s yaw=%.2f) — no snap."),
			Reason ? Reason : TEXT("(none)"), TargetYaw);
		return true;
	}
	const FRotator NewRot(Current.Pitch, TargetYaw, Current.Roll);
	Owner->SetActorRotation(NewRot);
	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("GodfreyPerformerBridge: snapped yaw to exhibition facing (reason=%s from=%.2f to=%.2f offset=%.2f)."),
		Reason ? Reason : TEXT("(none)"), Current.Yaw, TargetYaw, Bridge->ExhibitionBodyYawOffsetDegrees);
	return true;
}

static bool TryComputeRawExhibitionFacingYaw(const UGodfreyPerformerAnimationBridgeComponent* Bridge, float& OutRawYaw)
{
	if (!Bridge)
	{
		return false;
	}
	const AActor* const Owner = Bridge->GetOwner();
	if (!Owner)
	{
		return false;
	}

	FVector CameraLocation = FVector::ZeroVector;
	FRotator CameraRotation = FRotator::ZeroRotator;
	if (!TryGetExhibitionCameraTransform(const_cast<UGodfreyPerformerAnimationBridgeComponent*>(Bridge), CameraLocation, CameraRotation))
	{
		return false;
	}

	if (!Bridge->bFaceCameraViewDirection)
	{
		FVector Origin = Owner->GetActorLocation();
		Origin.Z += Bridge->ExhibitionFacingOriginHeightOffset;
		const FRotator LookAt = UKismetMathLibrary::FindLookAtRotation(Origin, CameraLocation);
		OutRawYaw = LookAt.Yaw;
		return true;
	}

	FVector ViewForward = CameraRotation.Vector();
	ViewForward.Z = 0.f;
	if (ViewForward.IsNearlyZero(KINDA_SMALL_NUMBER))
	{
		return false;
	}
	ViewForward.Normalize();
	OutRawYaw = (-ViewForward).Rotation().Yaw;
	return true;
}

static void LogOrientationSnapshot(UGodfreyPerformerAnimationBridgeComponent* Bridge, const TCHAR* Reason, const AActor* FocusTarget)
{
	if (!Bridge)
	{
		return;
	}
	const AActor* Owner = Bridge->GetOwner();
	if (!Owner)
	{
		return;
	}
	const float OwnerYaw = Owner->GetActorRotation().Yaw;
	float FacingYaw = 0.f;
	const bool bHasFacing = Bridge->TryGetExhibitionFacingYaw(FacingYaw);
	float RawFacingYaw = 0.f;
	const bool bHasRawFacing = TryComputeRawExhibitionFacingYaw(Bridge, RawFacingYaw);
	float LocYaw = 0.f;
	bool bHasLocYaw = false;
	FRotator CameraRotation = FRotator::ZeroRotator;
	FVector CameraLocation = FVector::ZeroVector;
	const bool bHasCameraTransform = TryGetExhibitionCameraTransform(Bridge, CameraLocation, CameraRotation);
	if (bHasCameraTransform)
	{
		FVector Delta = CameraLocation - Owner->GetActorLocation();
		Delta.Z = 0.f;
		if (!Delta.IsNearlyZero(1.f))
		{
			LocYaw = Delta.Rotation().Yaw;
			bHasLocYaw = true;
		}
	}
	float ViewYaw = 0.f;
	bool bHasViewYaw = false;
	if (bHasCameraTransform)
	{
		FVector ViewForward = CameraRotation.Vector();
		ViewForward.Z = 0.f;
		if (!ViewForward.IsNearlyZero(KINDA_SMALL_NUMBER))
		{
			ViewForward.Normalize();
			ViewYaw = (-ViewForward).Rotation().Yaw;
			bHasViewYaw = true;
		}
	}
	AActor* ResolvedCamera = ResolveExhibitionCameraActor(Bridge);
	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("GodfreyPerformerBridge: orient snapshot reason=%s ownerYaw=%.2f exhibitYaw=%.2f meshOffset=%.2f facingYaw=%s rawFacingYaw=%s locYaw=%s viewYaw=%s offset=%.2f lockExhibit=%d camActor=%s camLoc=%s focusTarget=%s"),
		Reason ? Reason : TEXT("(none)"),
		OwnerYaw,
		Bridge->HasCachedExhibitYaw() ? Bridge->GetCachedExhibitYawDegrees() : 0.f,
		Bridge->GetCachedMeshVisualYawOffsetDegrees(),
		bHasFacing ? *FString::Printf(TEXT("%.2f"), FacingYaw) : TEXT("(none)"),
		bHasRawFacing ? *FString::Printf(TEXT("%.2f"), RawFacingYaw) : TEXT("(none)"),
		bHasLocYaw ? *FString::Printf(TEXT("%.2f"), LocYaw) : TEXT("(none)"),
		bHasViewYaw ? *FString::Printf(TEXT("%.2f"), ViewYaw) : TEXT("(none)"),
		Bridge->ExhibitionBodyYawOffsetDegrees,
		Bridge->bLockBodyYawToExhibitFacingDuringConversation ? 1 : 0,
		ResolvedCamera ? *ResolvedCamera->GetName() : TEXT("(none)"),
		bHasCameraTransform
			? *FString::Printf(TEXT("(%.1f,%.1f,%.1f)"), CameraLocation.X, CameraLocation.Y, CameraLocation.Z)
			: TEXT("(none)"),
		FocusTarget ? *FocusTarget->GetName() : TEXT("(none)"));
}

static FString ResolveGazeSafeActionStem(const FString& Stem)
{
	const FString S = Stem.TrimStartAndEnd();
	const FString Lower = S.ToLower();
	if (S.Equals(TEXT("ThinkingLookingAway_01"), ESearchCase::IgnoreCase)
		|| S.Equals(TEXT("ThinkingLookingToSea_01"), ESearchCase::IgnoreCase)
		|| S.Equals(TEXT("ThinkingReturnGaze_01"), ESearchCase::IgnoreCase))
	{
		return TEXT("ThinkingHandToChin_01");
	}
	if (S.Equals(TEXT("LookToSea_01"), ESearchCase::IgnoreCase)
		|| S.Equals(TEXT("IdleLookingToSea_01"), ESearchCase::IgnoreCase)
		|| S.Equals(TEXT("IdleLookingToSea_02"), ESearchCase::IgnoreCase)
		|| S.Equals(TEXT("TransitionIdleToLookToSea_01"), ESearchCase::IgnoreCase))
	{
		return TEXT("IdleStanding_01");
	}
	if (S.Equals(TEXT("HandsClasped_01"), ESearchCase::IgnoreCase))
	{
		return TEXT("HandsBehindBack_01");
	}
	if (Lower.Contains(TEXT("lookingtosea"))
		|| Lower.Contains(TEXT("looktosea"))
		|| Lower.Contains(TEXT("lookingaway"))
		|| Lower.Contains(TEXT("reflectivepause"))
		|| Lower.Contains(TEXT("wereyouafraid"))
		|| Lower.Contains(TEXT("concerned")))
	{
		if (Lower.Contains(TEXT("thinking")))
		{
			return TEXT("ThinkingHandToChin_01");
		}
		if (Lower.Contains(TEXT("listening")))
		{
			return TEXT("ListeningAttentive_01");
		}
		if (Lower.Contains(TEXT("transition")))
		{
			return TEXT("TransitionSpeakingToIdle_01");
		}
		return TEXT("SpeakingCalmExplanation_01");
	}
	return S;
}

/** One anim-sequence cycle per dynamic speaking montage; section loop + tick rewind sustain speech. */
static constexpr int32 GodfreySpeakingIdleSegmentLoopCount = 1;
static constexpr float GestureIntensityDefault = 1.f;

float GetSpeakingIdleMontageBlendOut()
{
	const UUnrealPerformerGodfreySettings* const Settings = GetDefault<UUnrealPerformerGodfreySettings>();
	return FMath::Max(Settings->GodfreySpeakingIdleMontageBlendOutSeconds, Settings->GodfreyBodyMontageBlendOutSeconds);
}

float GetBodyMontageBlendIn()
{
	return GetDefault<UUnrealPerformerGodfreySettings>()->GodfreyBodyMontageBlendInSeconds;
}

float GetBodyMontageBlendOut()
{
	return GetDefault<UUnrealPerformerGodfreySettings>()->GodfreyBodyMontageBlendOutSeconds;
}

float GetDialogIdleMontageBlendIn()
{
	const UUnrealPerformerGodfreySettings* const Settings = GetDefault<UUnrealPerformerGodfreySettings>();
	return FMath::Max(Settings->GodfreyDialogIdleMontageBlendInSeconds, Settings->GodfreyBodyMontageBlendInSeconds);
}

float GetDialogIdleMontageBlendOut()
{
	const UUnrealPerformerGodfreySettings* const Settings = GetDefault<UUnrealPerformerGodfreySettings>();
	return FMath::Max(Settings->GodfreyDialogIdleMontageBlendOutSeconds, Settings->GodfreyBodyMontageBlendOutSeconds);
}

float GetSeaIdleMontageBlendIn()
{
	const UUnrealPerformerGodfreySettings* const Settings = GetDefault<UUnrealPerformerGodfreySettings>();
	return FMath::Max(Settings->GodfreySeaIdleMontageBlendInSeconds, Settings->GodfreyBodyMontageBlendInSeconds);
}

float GetSeaIdleMontageBlendOut()
{
	const UUnrealPerformerGodfreySettings* const Settings = GetDefault<UUnrealPerformerGodfreySettings>();
	return FMath::Max(Settings->GodfreySeaIdleMontageBlendOutSeconds, Settings->GodfreyBodyMontageBlendOutSeconds);
}

enum class EGodfreyIdleBlendProfile : uint8
{
	Body,
	DialogIdle,
	SeaIdle,
};

void ApplyBodyMontageBlendTimes(UAnimMontage* Montage, const EGodfreyIdleBlendProfile BlendProfile = EGodfreyIdleBlendProfile::Body)
{
	if (!Montage)
	{
		return;
	}
	float BlendIn = GetBodyMontageBlendIn();
	float BlendOut = GetBodyMontageBlendOut();
	if (BlendProfile == EGodfreyIdleBlendProfile::SeaIdle)
	{
		BlendIn = GetSeaIdleMontageBlendIn();
		BlendOut = GetSeaIdleMontageBlendOut();
	}
	else if (BlendProfile == EGodfreyIdleBlendProfile::DialogIdle)
	{
		BlendIn = GetDialogIdleMontageBlendIn();
		BlendOut = GetDialogIdleMontageBlendOut();
	}
	Montage->BlendIn.SetBlendTime(FMath::Max(0.05f, BlendIn));
	Montage->BlendOut.SetBlendTime(FMath::Max(0.05f, BlendOut));
}

const TCHAR* AnimTickOptionToString(const EVisibilityBasedAnimTickOption Option)
{
	switch (Option)
	{
	case EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones:
		return TEXT("AlwaysTick");
	case EVisibilityBasedAnimTickOption::AlwaysTickPose:
		return TEXT("AlwaysTickPose");
	case EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered:
		return TEXT("OnlyTickWhenRendered");
	case EVisibilityBasedAnimTickOption::OnlyTickMontagesWhenNotRendered:
		return TEXT("OnlyTickMontages");
	default:
		return TEXT("Unknown");
	}
}

#if WITH_EDITOR
struct FEditorViewportWatch
{
	float ViewDistance = -1.f;
	float ViewFOV = -1.f;
};

FEditorViewportWatch GetEditorViewportWatch(const USceneComponent* Component)
{
	FEditorViewportWatch Watch;
	if (!Component || !GEditor)
	{
		return Watch;
	}

	FLevelEditorViewportClient* ViewportClient = GCurrentLevelEditingViewportClient;
	if (!ViewportClient || !ViewportClient->IsPerspective())
	{
		for (FLevelEditorViewportClient* Client : GEditor->GetLevelViewportClients())
		{
			if (Client && Client->IsVisible() && Client->IsPerspective())
			{
				ViewportClient = Client;
				break;
			}
		}
	}

	if (ViewportClient)
	{
		Watch.ViewDistance = FVector::Dist(ViewportClient->GetViewLocation(), Component->GetComponentLocation());
		Watch.ViewFOV = ViewportClient->ViewFOV;
	}

	return Watch;
}
#endif

bool GetBoneWorldLocationSafe(const USkeletalMeshComponent* Mesh, const FName BoneName, FVector& OutLocation)
{
	OutLocation = FVector::ZeroVector;
	if (!IsValid(Mesh) || !Mesh->GetSkinnedAsset())
	{
		return false;
	}

	const int32 BoneIndex = Mesh->GetBoneIndex(BoneName);
	if (BoneIndex == INDEX_NONE)
	{
		return false;
	}

	OutLocation = Mesh->GetBoneLocation(BoneName, EBoneSpaces::WorldSpace);
	return FMath::IsFinite(OutLocation.X) && FMath::IsFinite(OutLocation.Y) && FMath::IsFinite(OutLocation.Z);
}

bool ReadPostProcessBoolProperty(UAnimInstance* PostProcessInstance, const TCHAR* PropertyName, bool& OutValue)
{
	OutValue = false;
	if (!PostProcessInstance)
	{
		return false;
	}

	return MetaHumanComponentHelpers::GetPropertyValue(PostProcessInstance, FStringView(PropertyName), OutValue);
}

float ComputeMaxWatchBoneDeltaCm(const USkeletalMeshComponent* Body, const USkeletalMeshComponent* Torso)
{
	float MaxDeltaCm = 0.f;
	if (!IsValid(Body) || !IsValid(Torso))
	{
		return MaxDeltaCm;
	}

	static const FName WatchBones[] = {
		FName(TEXT("pelvis")),
		FName(TEXT("spine_05")),
		FName(TEXT("clavicle_l")),
		FName(TEXT("clavicle_r")),
	};
	for (const FName BoneName : WatchBones)
	{
		FVector BodyPos = FVector::ZeroVector;
		FVector TorsoPos = FVector::ZeroVector;
		if (GetBoneWorldLocationSafe(Body, BoneName, BodyPos) && GetBoneWorldLocationSafe(Torso, BoneName, TorsoPos))
		{
			MaxDeltaCm = FMath::Max(MaxDeltaCm, FVector::Dist(BodyPos, TorsoPos));
		}
	}

	return MaxDeltaCm;
}

int32 ReadTorsoPostProcessBoolAsInt(const USkeletalMeshComponent* Torso, const TCHAR* PropertyName)
{
	bool bValue = false;
	if (IsValid(Torso) && Torso->GetPostProcessInstance())
	{
		ReadPostProcessBoolProperty(Torso->GetPostProcessInstance(), PropertyName, bValue);
	}
	return bValue ? 1 : 0;
}

UClass* LoadClothingPostProcessAnimClass()
{
	UClass* ClothingPostProcessClass = StaticLoadClass(
		UAnimInstance::StaticClass(),
		nullptr,
		TEXT("/Game/MetaHumans/Common/Shared/Animation/ABP_Clothing_PostProcess.ABP_Clothing_PostProcess_C"));
	if (!ClothingPostProcessClass)
	{
		ClothingPostProcessClass = StaticLoadClass(
			UAnimInstance::StaticClass(),
			nullptr,
			TEXT("/Game/MetaHumans/Common/Animation/ABP_Clothing_PostProcess.ABP_Clothing_PostProcess_C"));
	}
	return ClothingPostProcessClass;
}

void WireAnimInstanceObjectProperty(UAnimInstance* AnimInstance, const FName PropertyName, UObject* Value)
{
	if (!AnimInstance || !Value)
	{
		return;
	}

	if (FObjectProperty* ObjectProperty = FindFProperty<FObjectProperty>(AnimInstance->GetClass(), PropertyName))
	{
		ObjectProperty->SetObjectPropertyValue_InContainer(AnimInstance, Value);
	}
}

bool WireAnimInstanceSkeletalMeshComponentPropertyRecursive(UStruct* Struct, void* Container,
	USkeletalMeshComponent* Value, const int32 Depth, FName* OutPropertyName)
{
	if (!Struct || !Container || !Value || Depth > 12)
	{
		return false;
	}

	for (TFieldIterator<FProperty> It(Struct, EFieldIteratorFlags::IncludeSuper); It; ++It)
	{
		if (FObjectProperty* ObjectProperty = CastField<FObjectProperty>(*It))
		{
			if (!ObjectProperty->PropertyClass->IsChildOf(USkeletalMeshComponent::StaticClass()))
			{
				continue;
			}

			ObjectProperty->SetObjectPropertyValue_InContainer(Container, Value);
			if (OutPropertyName)
			{
				*OutPropertyName = It->GetFName();
			}
			return true;
		}

		if (FStructProperty* StructProperty = CastField<FStructProperty>(*It))
		{
			void* StructValue = StructProperty->ContainerPtrToValuePtr<void>(Container);
			if (WireAnimInstanceSkeletalMeshComponentPropertyRecursive(
					StructProperty->Struct, StructValue, Value, Depth + 1, OutPropertyName))
			{
				return true;
			}
		}
	}

	return false;
}

bool WireAnimInstanceSkeletalMeshComponentProperty(UAnimInstance* AnimInstance, USkeletalMeshComponent* Value,
	const TArrayView<const FName> PreferredNames, FName* OutPropertyName = nullptr)
{
	if (!AnimInstance || !Value)
	{
		return false;
	}

	auto TryWire = [AnimInstance, Value, OutPropertyName](const FName PropertyName) -> bool
	{
		if (FObjectProperty* ObjectProperty = FindFProperty<FObjectProperty>(AnimInstance->GetClass(), PropertyName))
		{
			if (!ObjectProperty->PropertyClass->IsChildOf(USkeletalMeshComponent::StaticClass()))
			{
				return false;
			}

			ObjectProperty->SetObjectPropertyValue_InContainer(AnimInstance, Value);
			if (OutPropertyName)
			{
				*OutPropertyName = PropertyName;
			}
			return true;
		}

		return false;
	};

	for (const FName PropertyName : PreferredNames)
	{
		if (TryWire(PropertyName))
		{
			return true;
		}
	}

	if (WireAnimInstanceSkeletalMeshComponentPropertyRecursive(
			AnimInstance->GetClass(), AnimInstance, Value, 0, OutPropertyName))
	{
		return true;
	}

	return false;
}

static bool GarmentMeshHasPostProcessAnim(const USkeletalMeshComponent* Follower)
{
	if (!IsValid(Follower))
	{
		return false;
	}

	if (Follower->GetPostProcessInstance())
	{
		return true;
	}

	const USkeletalMesh* MeshAsset = Follower->GetSkeletalMeshAsset();
	return IsValid(MeshAsset) && MeshAsset->GetPostProcessAnimBlueprint() != nullptr;
}

void ApplySpeakingMontageSectionLoop(UAnimInstance* AnimInst, UAnimMontage* PlayMontage)
{
	if (!AnimInst || !PlayMontage || PlayMontage->CompositeSections.Num() == 0)
	{
		return;
	}

	const FName LoopSection = PlayMontage->CompositeSections[0].SectionName;
	for (const FCompositeSection& Section : PlayMontage->CompositeSections)
	{
		AnimInst->Montage_SetNextSection(Section.SectionName, LoopSection, PlayMontage);
	}
}

static bool IsExcludedFaceMesh(const USkeletalMeshComponent* Mesh, const FString& FaceExclude)
{
	if (!IsValid(Mesh) || FaceExclude.IsEmpty())
	{
		return false;
	}
	return Mesh->GetName().Contains(FaceExclude, ESearchCase::IgnoreCase);
}

static bool HasRenderableSkeletalMeshAsset(const USkeletalMeshComponent* Mesh)
{
	return IsValid(Mesh) && Mesh->GetSkeletalMeshAsset() != nullptr;
}

bool IsDriveableSkeletalMesh(const USkeletalMeshComponent* Mesh)
{
	return IsValid(Mesh) && Mesh->IsRegistered() && HasRenderableSkeletalMeshAsset(Mesh);
}

void ForceSkeletalMeshPoseRefresh(USkeletalMeshComponent* Mesh)
{
	if (!IsDriveableSkeletalMesh(Mesh))
	{
		return;
	}

	Mesh->TickAnimation(0.f, false);
	Mesh->TickComponent(0.f, ELevelTick::LEVELTICK_All, nullptr);
	Mesh->RefreshBoneTransforms(nullptr);
}

void RefreshMetaHumanBodyPoseChain(USkeletalMeshComponent* Body)
{
	if (!IsDriveableSkeletalMesh(Body))
	{
		return;
	}

	Body->TickAnimation(0.f, false);
	Body->TickComponent(0.f, ELevelTick::LEVELTICK_All, nullptr);
	Body->RefreshBoneTransforms(nullptr);
	Body->RefreshFollowerComponents();
}

void RefreshMetaHumanGarmentPostProcessPose(USkeletalMeshComponent* Body, USkeletalMeshComponent* Garment)
{
	if (!IsDriveableSkeletalMesh(Body) || !IsDriveableSkeletalMesh(Garment))
	{
		return;
	}

	RefreshMetaHumanBodyPoseChain(Body);

	Garment->SetDisablePostProcessBlueprint(false);
	Garment->TickAnimation(0.f, false);
	Garment->TickComponent(0.f, ELevelTick::LEVELTICK_All, nullptr);
	Garment->RefreshBoneTransforms(nullptr);
}

static int32 ScoreBodyMeshCandidate(const USkeletalMeshComponent* Mesh, const FString& BodyHint)
{
	if (!HasRenderableSkeletalMeshAsset(Mesh))
	{
		return -1;
	}

	const FString Name = Mesh->GetName();
	if (Name.Equals(BodyHint, ESearchCase::IgnoreCase))
	{
		return 100;
	}
	if (Name.Contains(BodyHint, ESearchCase::IgnoreCase))
	{
		return 50;
	}
	return 1;
}

static bool MontageHasPlayableSlotTrack(const UAnimMontage* Montage, const FName SlotName)
{
	if (!Montage)
	{
		return false;
	}

	for (const FSlotAnimationTrack& Track : Montage->SlotAnimTracks)
	{
		if (Track.SlotName == SlotName && Track.AnimTrack.AnimSegments.Num() > 0)
		{
			return true;
		}
	}
	return false;
}

static FString DescribeMontageSlotTracks(const UAnimMontage* Montage)
{
	if (!Montage)
	{
		return TEXT("(null montage)");
	}

	if (Montage->SlotAnimTracks.Num() == 0)
	{
		return TEXT("(no slot tracks)");
	}

	FString Result;
	for (const FSlotAnimationTrack& Track : Montage->SlotAnimTracks)
	{
		if (!Result.IsEmpty())
		{
			Result += TEXT(", ");
		}
		Result += FString::Printf(TEXT("'%s'(%d)"), *Track.SlotName.ToString(), Track.AnimTrack.AnimSegments.Num());
	}
	return Result;
}

static UAnimSequence* ExtractPrimarySequenceFromMontage(const UAnimMontage* Montage)
{
	if (!Montage)
	{
		return nullptr;
	}

	for (const FSlotAnimationTrack& Track : Montage->SlotAnimTracks)
	{
		for (const FAnimSegment& Segment : Track.AnimTrack.AnimSegments)
		{
			if (const UAnimSequenceBase* AnimRef = Segment.GetAnimReference())
			{
				if (UAnimSequence* Sequence = Cast<UAnimSequence>(const_cast<UAnimSequenceBase*>(AnimRef)))
				{
					return Sequence;
				}
			}
		}
	}

	if (const UAnimSequenceBase* FirstRef = Montage->GetFirstAnimReference())
	{
		return Cast<UAnimSequence>(const_cast<UAnimSequenceBase*>(FirstRef));
	}

	return nullptr;
}

static UAnimSequence* LoadAnimSequenceAssetByPath(const FString& PackagePath)
{
	if (PackagePath.IsEmpty())
	{
		return nullptr;
	}

	const FString ShortName = FPackageName::GetShortName(PackagePath);
	const FString ObjectPath = PackagePath.Contains(TEXT("."))
		? PackagePath
		: FString::Printf(TEXT("%s.%s"), *PackagePath, *ShortName);

	if (UAnimSequence* Existing = FindObject<UAnimSequence>(nullptr, *ObjectPath))
	{
		return Existing;
	}
	if (UAnimSequence* Loaded = LoadObject<UAnimSequence>(nullptr, *ObjectPath))
	{
		return Loaded;
	}
	return Cast<UAnimSequence>(FSoftObjectPath(ObjectPath).TryLoad());
}

void ForEachCopyPoseFromMeshNode(UStruct* Struct, void* Container,
	const TFunctionRef<void(FAnimNode_CopyPoseFromMesh*)>& Func)
{
	if (!Struct || !Container)
	{
		return;
	}

	for (TFieldIterator<FProperty> It(Struct, EFieldIteratorFlags::IncludeSuper); It; ++It)
	{
		if (FStructProperty* StructProp = CastField<FStructProperty>(*It))
		{
			void* StructValue = StructProp->ContainerPtrToValuePtr<void>(Container);
			if (StructProp->Struct == FAnimNode_CopyPoseFromMesh::StaticStruct())
			{
				Func(static_cast<FAnimNode_CopyPoseFromMesh*>(StructValue));
			}
			else
			{
				ForEachCopyPoseFromMeshNode(StructProp->Struct, StructValue, Func);
			}
		}
	}
}

int32 WireCopyPoseFromMeshAnimGraphNodes(UAnimInstance* AnimInstance, USkeletalMeshComponent* Body)
{
	if (!AnimInstance || !Body)
	{
		return 0;
	}

	int32 WiredCount = 0;
	ForEachCopyPoseFromMeshNode(AnimInstance->GetClass(), AnimInstance, [&](FAnimNode_CopyPoseFromMesh* Node)
	{
		if (!Node)
		{
			return;
		}

		Node->SourceMeshComponent = Body;
		Node->bUseAttachedParent = true;
		++WiredCount;
	});
	return WiredCount;
}

const FMetaHumanCustomizableBodyPart* GetMetaHumanBodyPartConfig(
	const UMetaHumanComponentUE* MetaHumanComponent, const FName GarmentLabel)
{
	if (!MetaHumanComponent)
	{
		return nullptr;
	}

	if (const FStructProperty* StructProp = FindFProperty<FStructProperty>(
		MetaHumanComponent->GetClass(), GarmentLabel))
	{
		if (StructProp->Struct == FMetaHumanCustomizableBodyPart::StaticStruct())
		{
			return StructProp->ContainerPtrToValuePtr<FMetaHumanCustomizableBodyPart>(MetaHumanComponent);
		}
	}

	return nullptr;
}

void MirrorMetaHumanPostConnectAnimBPVariables(
	const FMetaHumanCustomizableBodyPart& BodyPart, UAnimInstance* AnimInstance)
{
	if (!AnimInstance)
	{
		return;
	}

	MetaHumanComponentHelpers::ConnectVariable<FBoolProperty, bool>(
		AnimInstance, TEXT("Enable Control Rig"), BodyPart.ControlRigClass.Get() != nullptr);

	if (BodyPart.ControlRigClass)
	{
		MetaHumanComponentHelpers::ConnectVariable<FObjectProperty, TSubclassOf<UControlRig>>(
			AnimInstance, TEXT("Control Rig Class"), BodyPart.ControlRigClass);
		MetaHumanComponentHelpers::ConnectVariable<FIntProperty, int32>(
			AnimInstance, TEXT("Control Rig LOD Threshold"), BodyPart.ControlRigLODThreshold);
	}

	MetaHumanComponentHelpers::ConnectVariable<FBoolProperty, bool>(
		AnimInstance, TEXT("Enable Rigid Body Simulation"), BodyPart.PhysicsAsset.Get() != nullptr);

	if (BodyPart.PhysicsAsset)
	{
		MetaHumanComponentHelpers::ConnectVariable<FObjectProperty, TObjectPtr<UPhysicsAsset>>(
			AnimInstance, TEXT("Override Physics Asset"), BodyPart.PhysicsAsset);
		MetaHumanComponentHelpers::ConnectVariable<FIntProperty, int32>(
			AnimInstance, TEXT("Rigid Body LOD Threshold"), BodyPart.RigidBodyLODThreshold);
	}
}

void ApplyMetaHumanCopyPoseBodyVisibility(USkeletalMeshComponent* Body, const bool bEditorViewportWorld,
	const bool bForceBodyVisible)
{
	if (!IsValid(Body))
	{
		return;
	}

	if (bForceBodyVisible)
	{
		Body->SetHiddenInGame(false, true);
	}
	else
	{
#if WITH_EDITOR
		Body->SetHiddenInGame(bEditorViewportWorld ? false : true, true);
#else
		Body->SetHiddenInGame(true, true);
#endif
	}
	Body->SetVisibility(true, true);
}
} // namespace

UGodfreyPerformerAnimationBridgeComponent::UGodfreyPerformerAnimationBridgeComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	bWantsInitializeComponent = true;

	const TCHAR* const LibraryRoot = TEXT("/Game/Godfrey/Animation/Animation/Performances");
	auto SoftSeq = [LibraryRoot](const TCHAR* AssetName) -> TSoftObjectPtr<UAnimSequence>
	{
		const FString Path = FString::Printf(TEXT("%s/%s.%s"), LibraryRoot, AssetName, AssetName);
		return TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(Path));
	};

	DefaultListeningSequence = SoftSeq(TEXT("AS_ListeningAttentive_01"));
	DefaultThinkingSequence = SoftSeq(TEXT("AS_ThinkingHandToChin_01"));
	DefaultSpeakingStartSequence = SoftSeq(TEXT("AS_SpeakingGentleEmphasis_01"));
	DefaultSpeakingIdleSequence = SoftSeq(TEXT("AS_SpeakingCalmExplanation_01"));
	DefaultEmphasisSequence = SoftSeq(TEXT("AS_SpeakingGentleEmphasis_01"));
	DefaultAmusedSequence = SoftSeq(TEXT("AS_Laughing_01"));
	DefaultSeriousSequence = SoftSeq(TEXT("AS_Concerned_01"));
	DefaultReturnToIdleSequence = SoftSeq(TEXT("AS_IdleStanding_01"));
	DefaultIdleBreathingSequence = SoftSeq(TEXT("AS_IdleWeightShift_01"));
	DefaultSeaIdleSequence = SoftSeq(TEXT("AS_IdleLookingToSea_01"));
	DefaultEngageTurnSequence = SoftSeq(TEXT("AS_GreetingTurnToVisitor_01"));
	DefaultEngageGreetSequence = SoftSeq(TEXT("AS_GreetingWelcome_01"));
	DefaultFarewellWaveSequence = SoftSeq(TEXT("AS_FarewellWave_01"));
	DefaultBackToSeaSequence = SoftSeq(TEXT("AS_TransitionIdleToLookToSea_01"));
}

void UGodfreyPerformerAnimationBridgeComponent::InitializeComponent()
{
	Super::InitializeComponent();
	UpdatePerformerTickEnabled();

#if WITH_EDITOR
	if (IsBridgeActiveForMetaHumanIntervention()
		&& ShouldManageMetaHumanGarmentsAtRuntime()
		&& IsEditorViewportWorld()
		&& UsesMetaHumanNativeClothingPipeline()
		&& HasMetaHumanGarmentPostProcessMesh())
	{
		if (UWorld* World = GetWorld())
		{
			ScheduleEditorCopyPoseStabilize();
		}
	}
	else if (IsBridgeActiveForMetaHumanIntervention()
		&& !ShouldManageMetaHumanGarmentsAtRuntime()
		&& IsEditorViewportWorld()
		&& UsesMetaHumanNativeClothingPipeline())
	{
		PinEditorViewportMetaHumanLOD();
	}
#endif
}

void UGodfreyPerformerAnimationBridgeComponent::ScheduleEditorCopyPoseStabilize()
{
	if (bEditorCopyPoseStabilizeScheduled)
	{
		return;
	}

	bEditorCopyPoseStabilizeScheduled = true;

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateUObject(
			this, &UGodfreyPerformerAnimationBridgeComponent::TryStabilizeClothingForEditorViewport));
		ScheduleMetaHumanClothingRefreshPasses();
	}
}

void UGodfreyPerformerAnimationBridgeComponent::OnRegister()
{
	Super::OnRegister();
	UpdatePerformerTickEnabled();

#if WITH_EDITOR
	if (IsBridgeActiveForMetaHumanIntervention()
		&& ShouldManageMetaHumanGarmentsAtRuntime()
		&& IsEditorViewportWorld()
		&& UsesMetaHumanNativeClothingPipeline()
		&& HasMetaHumanGarmentPostProcessMesh())
	{
		ScheduleEditorCopyPoseStabilize();
	}
	else if (IsBridgeActiveForMetaHumanIntervention()
		&& !ShouldManageMetaHumanGarmentsAtRuntime()
		&& IsEditorViewportWorld()
		&& UsesMetaHumanNativeClothingPipeline())
	{
		PinEditorViewportMetaHumanLOD();
	}
#endif
}

bool UGodfreyPerformerAnimationBridgeComponent::IsBridgeActiveForMetaHumanIntervention() const
{
	return bAutoActivate;
}

bool UGodfreyPerformerAnimationBridgeComponent::IsEditorViewportWorld() const
{
	const UWorld* World = GetWorld();
	return World && World->WorldType == EWorldType::Editor;
}

bool UGodfreyPerformerAnimationBridgeComponent::UsesMetaHumanNativeClothingPipeline() const
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return false;
	}

	static UClass* MetaHumanComponentClass = nullptr;
	if (!MetaHumanComponentClass)
	{
		MetaHumanComponentClass = LoadClass<UActorComponent>(
			nullptr,
			TEXT("/Script/MetaHumanSDKRuntime.MetaHumanComponentUE"));
	}

	return MetaHumanComponentClass && Owner->FindComponentByClass(MetaHumanComponentClass) != nullptr;
}

bool UGodfreyPerformerAnimationBridgeComponent::ShouldManageClothingLeaderPose() const
{
	return bAutoWireClothingLeaderPoseToBody && !UsesMetaHumanNativeClothingPipeline();
}

bool UGodfreyPerformerAnimationBridgeComponent::ShouldManageMetaHumanGarmentsAtRuntime() const
{
	return bManageMetaHumanGarmentsAtRuntime && UsesMetaHumanNativeClothingPipeline();
}

bool UGodfreyPerformerAnimationBridgeComponent::HasMetaHumanGarmentPostProcessMesh() const
{
	if (!UsesMetaHumanNativeClothingPipeline())
	{
		return false;
	}

	// Shirt copy-pose path applies to Torso only; Feet shoe post-process must not enable it (Erno leader-pose shirt).
	const USkeletalMeshComponent* Torso = FindFollowerMeshByComponentName(FName(TEXT("Torso")));
	return GarmentMeshHasPostProcessAnim(Torso);
}

void UGodfreyPerformerAnimationBridgeComponent::EnsureMetaHumanFaceVisible()
{
	USkeletalMeshComponent* Face = FindFollowerMeshByComponentName(FName(TEXT("Face")));
	if (!IsValid(Face))
	{
		return;
	}

	Face->SetHiddenInGame(false, true);
	Face->SetVisibility(true, true);
	Face->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	Face->bEnableUpdateRateOptimizations = false;

#if WITH_EDITOR
	Face->SetUpdateAnimationInEditor(true);
#endif
}

void UGodfreyPerformerAnimationBridgeComponent::EnsureMetaHumanCopyPoseGarmentAttachedToBody(
	USkeletalMeshComponent* Body, USkeletalMeshComponent* Garment)
{
	if (!IsDriveableSkeletalMesh(Body) || !IsDriveableSkeletalMesh(Garment) || Garment == Body)
	{
		return;
	}

	if (Garment->GetAttachParent() == Body)
	{
		return;
	}

	Garment->AttachToComponent(Body, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("GodfreyPerformerBridge: attached '%s' -> Body '%s' (CopyPose bUseAttachedParent fallback)."),
		*Garment->GetName(),
		*Body->GetName());
}

void UGodfreyPerformerAnimationBridgeComponent::ClearPostProcessGarmentLeaderPose(USkeletalMeshComponent* Garment)
{
	if (!IsValid(Garment) || !Garment->LeaderPoseComponent.IsValid())
	{
		return;
	}

	const USkinnedMeshComponent* PreviousLeader = Garment->LeaderPoseComponent.Get();
	Garment->SetLeaderPoseComponent(nullptr, false, true);
	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("GodfreyPerformerBridge: cleared leader pose on '%s' (was '%s'; shirt uses post-process CopyPose, not leader)."),
		*Garment->GetName(),
		PreviousLeader ? *PreviousLeader->GetName() : TEXT("(none)"));
}

void UGodfreyPerformerAnimationBridgeComponent::WireClothingPostProcessCopyPoseSource(
	USkeletalMeshComponent* Body, USkeletalMeshComponent* Garment, const bool bLogWiringResult)
{
	if (!IsValid(Body) || !IsValid(Garment))
	{
		return;
	}

	EnsureMetaHumanCopyPoseGarmentAttachedToBody(Body, Garment);

	if (!Garment->GetPostProcessInstance() && Garment->IsRegistered())
	{
		Garment->InitializeAnimScriptInstance(false, false);
	}

	UAnimInstance* const PostProcessInstance = Garment->GetPostProcessInstance();
	if (!PostProcessInstance)
	{
		UE_LOG(LogGodfreyPerformance, Warning,
			TEXT("GodfreyPerformerBridge: no post-process AnimInstance on '%s' — cannot wire CopyPose source yet."),
			*Garment->GetName());
		return;
	}

	RefreshMetaHumanBodyPoseChain(Body);

	const int32 AnimGraphNodesWired = WireCopyPoseFromMeshAnimGraphNodes(PostProcessInstance, Body);

	static const FName PreferredPropertyNames[] = {
		TEXT("Source Mesh Component"),
		TEXT("SourceMeshComponent"),
		TEXT("Mesh to Copy"),
		TEXT("MeshToCopy"),
		TEXT("Body Mesh"),
		TEXT("BodyMesh"),
		TEXT("Source Mesh"),
		TEXT("SkeletalMeshToCopy"),
	};

	FName WiredPropertyName;
	const bool bWiredUObjectProperty = WireAnimInstanceSkeletalMeshComponentProperty(
		PostProcessInstance, Body, PreferredPropertyNames, &WiredPropertyName);

	ApplyMetaHumanGarmentPostProcessVariables(Garment, PostProcessInstance);

	Garment->SetDisablePostProcessBlueprint(false);

	if (bLogWiringResult)
	{
		if (AnimGraphNodesWired > 0)
		{
			if (Garment->GetFName() == FName(TEXT("Torso")))
			{
				if (!bLoggedCopyPoseWireForTorso)
				{
					bLoggedCopyPoseWireForTorso = true;
					UE_LOG(LogGodfreyPerformance, Log,
						TEXT("GodfreyPerformerBridge: CopyPose anim graph wired (%d node(s)) on '%s' -> Body '%s' (SourceMeshComponent + bUseAttachedParent)."),
						AnimGraphNodesWired,
						*Garment->GetName(),
						*Body->GetName());
				}
			}
			else
			{
				UE_LOG(LogGodfreyPerformance, Log,
					TEXT("GodfreyPerformerBridge: CopyPose anim graph wired (%d node(s)) on '%s' -> Body '%s' (SourceMeshComponent + bUseAttachedParent)."),
					AnimGraphNodesWired,
					*Garment->GetName(),
					*Body->GetName());
			}
		}
		else if (bWiredUObjectProperty)
		{
			UE_LOG(LogGodfreyPerformance, Log,
				TEXT("GodfreyPerformerBridge: CopyPose source '%s' on '%s' -> Body '%s'."),
				*WiredPropertyName.ToString(),
				*Garment->GetName(),
				*Body->GetName());
		}
		else
		{
			UE_LOG(LogGodfreyPerformance, Log,
				TEXT("GodfreyPerformerBridge: CopyPose on '%s' has no UObject source pin — relying on attach-parent Body '%s' (attachParent='%s')."),
				*Garment->GetName(),
				*Body->GetName(),
				Garment->GetAttachParent() ? *Garment->GetAttachParent()->GetName() : TEXT("(none)"));
		}
	}

	RefreshMetaHumanGarmentPostProcessPose(Body, Garment);
}

void UGodfreyPerformerAnimationBridgeComponent::ApplyMetaHumanGarmentPostProcessVariables(
	USkeletalMeshComponent* Garment, UAnimInstance* PostProcessInstance)
{
	if (!IsValid(Garment) || !PostProcessInstance)
	{
		return;
	}

	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	const UMetaHumanComponentUE* MetaHumanComponent = Owner->FindComponentByClass<UMetaHumanComponentUE>();
	if (!MetaHumanComponent)
	{
		return;
	}

	const FName GarmentLabel(*Garment->GetName());
	const FMetaHumanCustomizableBodyPart* BodyPartConfig = GetMetaHumanBodyPartConfig(MetaHumanComponent, GarmentLabel);
	if (!BodyPartConfig)
	{
		return;
	}

	FMetaHumanCustomizableBodyPart EffectivePart = *BodyPartConfig;
	if (const UAnimInstance* PPDefault = Cast<UAnimInstance>(PostProcessInstance->GetClass()->GetDefaultObject()))
	{
		if (!EffectivePart.ControlRigClass)
		{
			MetaHumanComponentHelpers::GetPropertyValue(
				const_cast<UAnimInstance*>(PPDefault), TEXTVIEW("Control Rig Class"), EffectivePart.ControlRigClass);
		}

		if (!EffectivePart.PhysicsAsset)
		{
			MetaHumanComponentHelpers::GetPropertyValue(
				const_cast<UAnimInstance*>(PPDefault), TEXTVIEW("Override Physics Asset"), EffectivePart.PhysicsAsset);
		}
	}

	if (bPinClothingToSkinnedPose)
	{
		static constexpr int32 NeverActiveLODThreshold = 999;
		MetaHumanComponentHelpers::ConnectVariable<FBoolProperty, bool>(
			PostProcessInstance, TEXT("Enable Control Rig"), false);
		MetaHumanComponentHelpers::ConnectVariable<FBoolProperty, bool>(
			PostProcessInstance, TEXT("Enable Rigid Body Simulation"), false);
		MetaHumanComponentHelpers::ConnectVariable<FIntProperty, int32>(
			PostProcessInstance, TEXT("Control Rig LOD Threshold"), NeverActiveLODThreshold);
		MetaHumanComponentHelpers::ConnectVariable<FIntProperty, int32>(
			PostProcessInstance, TEXT("Rigid Body LOD Threshold"), NeverActiveLODThreshold);
	}
	else
	{
		MirrorMetaHumanPostConnectAnimBPVariables(EffectivePart, PostProcessInstance);
	}

#if WITH_EDITOR
	if (IsEditorViewportWorld() && GarmentLabel == FName(TEXT("Torso")))
	{
		ApplyEditorTorsoCopyPoseOnlyOverrides(PostProcessInstance);
	}
#endif
}

bool UGodfreyPerformerAnimationBridgeComponent::AreMetaHumanGarmentMeshesRegistered() const
{
	if (IsValid(TargetSkeletalMesh) && HasRenderableSkeletalMeshAsset(TargetSkeletalMesh)
		&& !TargetSkeletalMesh->IsRegistered())
	{
		return false;
	}

	for (const FName FollowerName : ClothingFollowerMeshNames)
	{
		const USkeletalMeshComponent* Follower = FindFollowerMeshByComponentName(FollowerName);
		if (!IsValid(Follower) || !HasRenderableSkeletalMeshAsset(Follower))
		{
			continue;
		}

		if (!Follower->IsRegistered())
		{
			return false;
		}
	}

	return true;
}

void UGodfreyPerformerAnimationBridgeComponent::RefreshMetaHumanGarmentPoses(USkeletalMeshComponent* Body)
{
	if (!IsDriveableSkeletalMesh(Body))
	{
		return;
	}

	Body->TickAnimation(0.f, false);
	Body->RefreshBoneTransforms();

	for (const FName FollowerName : ClothingFollowerMeshNames)
	{
		USkeletalMeshComponent* Follower = FindFollowerMeshByComponentName(FollowerName);
		if (!IsDriveableSkeletalMesh(Follower) || Follower == Body)
		{
			continue;
		}

		if (GarmentMeshHasPostProcessAnim(Follower))
		{
			ClearPostProcessGarmentLeaderPose(Follower);
			WireClothingPostProcessCopyPoseSource(Body, Follower);
			ForceSkeletalMeshPoseRefresh(Follower);
		}
		else if (Follower->LeaderPoseComponent.Get() == Body)
		{
			Follower->UpdateFollowerComponent();
		}
		else
		{
			ForceSkeletalMeshPoseRefresh(Follower);
		}
	}

	Body->RefreshFollowerComponents();
}

void UGodfreyPerformerAnimationBridgeComponent::ScheduleMetaHumanClothingRefreshPasses()
{
	if (bMetaHumanClothingRefreshPassesScheduled)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	bMetaHumanClothingRefreshPassesScheduled = true;
	MetaHumanClothingRefreshPassCount = 0;

	FTimerDelegate Delegate = FTimerDelegate::CreateUObject(
		this, &UGodfreyPerformerAnimationBridgeComponent::DeferredMetaHumanClothingRefresh);

	World->GetTimerManager().SetTimerForNextTick(Delegate);

	FTimerHandle DelayedRefreshHandle;
	World->GetTimerManager().SetTimer(DelayedRefreshHandle, Delegate, 0.05f, false);

	FTimerHandle LateRefreshHandle;
	World->GetTimerManager().SetTimer(LateRefreshHandle, Delegate, 0.15f, false);

	FTimerHandle PieSettleHandle;
	World->GetTimerManager().SetTimer(PieSettleHandle, Delegate, 0.5f, false);

	FTimerHandle PieLateHandle;
	World->GetTimerManager().SetTimer(PieLateHandle, Delegate, 1.0f, false);

	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("GodfreyPerformerBridge: scheduled %d MetaHuman garment refresh passes."),
		MetaHumanMaxClothingRefreshPasses);
}

void UGodfreyPerformerAnimationBridgeComponent::DeferredMetaHumanClothingRefresh()
{
	if (!UsesMetaHumanNativeClothingPipeline())
	{
		return;
	}

	if (bAutoResolveMetaHumanBodyMesh && ShouldAutoResolveBodyMesh())
	{
		ResolveTargetBodyMesh();
	}

	if (!AreMetaHumanGarmentMeshesRegistered())
	{
		if (MetaHumanGarmentRegisterWaitFrames < MetaHumanMaxGarmentRegisterWaitFrames)
		{
			++MetaHumanGarmentRegisterWaitFrames;
			if (UWorld* World = GetWorld())
			{
				World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateUObject(
					this, &UGodfreyPerformerAnimationBridgeComponent::DeferredMetaHumanClothingRefresh));
			}
		}
		return;
	}

	MetaHumanGarmentRegisterWaitFrames = 0;

	if (MetaHumanClothingRefreshPassCount >= MetaHumanMaxClothingRefreshPasses)
	{
		return;
	}

	++MetaHumanClothingRefreshPassCount;
	PinClothingToSkinnedPose();

	// MetaHumanComponentUE::BeginPlay runs after this component and resets OnlyTickPoseWhenRendered.
	if (ShouldManageMetaHumanGarmentsAtRuntime() && HasMetaHumanGarmentPostProcessMesh())
	{
		EnsureMetaHumanCopyPoseBodySource();
	}

	if (ShouldManageMetaHumanGarmentsAtRuntime())
	{
		EnsureMetaHumanBodyTicksForClothingPostProcess();
	}

	if (!IsValid(TargetSkeletalMesh))
	{
		return;
	}

	RefreshMetaHumanGarmentPoses(TargetSkeletalMesh);

	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("GodfreyPerformerBridge: MetaHuman garment refresh pass %d complete."),
		MetaHumanClothingRefreshPassCount);

	if (MetaHumanClothingRefreshPassCount == 1)
	{
		LogSkeletalMeshPropagationReport();
	}

#if WITH_EDITOR
	ReapplyEditorGarmentPreviewSettings();
#endif
}

void UGodfreyPerformerAnimationBridgeComponent::ApplyMetaHumanClothingTickPrerequisites(
	USkeletalMeshComponent* Body)
{
	if (!IsValid(Body) || bMetaHumanClothingTickPrerequisitesApplied)
	{
		return;
	}

	for (const FName FollowerName : ClothingFollowerMeshNames)
	{
		USkeletalMeshComponent* Follower = FindFollowerMeshByComponentName(FollowerName);
		if (!IsValid(Follower) || Follower == Body)
		{
			continue;
		}

		Follower->AddTickPrerequisiteComponent(Body);
	}

	bMetaHumanClothingTickPrerequisitesApplied = true;

	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("GodfreyPerformerBridge: garment mesh tick prerequisites -> Body '%s'."),
		*Body->GetName());
}

void UGodfreyPerformerAnimationBridgeComponent::TryStabilizeClothingForEditorViewport()
{
	const UWorld* World = GetWorld();
	if (!World || World->IsGameWorld())
	{
		return;
	}

#if WITH_EDITOR
	const bool bCopyPoseGarment = HasMetaHumanGarmentPostProcessMesh();
	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("GodfreyPerformerBridge: editor stabilize on '%s' (copyPoseGarment=%d metaHuman=%d)."),
		GetOwner() ? *GetOwner()->GetName() : TEXT("(no owner)"),
		bCopyPoseGarment ? 1 : 0,
		UsesMetaHumanNativeClothingPipeline() ? 1 : 0);

	if (ShouldManageMetaHumanGarmentsAtRuntime() && bCopyPoseGarment)
	{
		if (bCopyPoseGarment)
		{
			if (bAutoResolveMetaHumanBodyMesh && ShouldAutoResolveBodyMesh())
			{
				ResolveTargetBodyMesh();
			}

			if (!AreMetaHumanGarmentMeshesRegistered())
			{
				if (MetaHumanGarmentRegisterWaitFrames < MetaHumanMaxGarmentRegisterWaitFrames)
				{
					++MetaHumanGarmentRegisterWaitFrames;
					if (UWorld* MutableWorld = GetWorld())
					{
						MutableWorld->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateUObject(
							this, &UGodfreyPerformerAnimationBridgeComponent::TryStabilizeClothingForEditorViewport));
					}
				}
				return;
			}

			MetaHumanGarmentRegisterWaitFrames = 0;
			EnsureMetaHumanCopyPoseBodySource();
			RestoreEditorMetaHumanViewportDefaults();
			RefreshMetaHumanGarmentPoses(TargetSkeletalMesh);
			MaybeLogMetaHumanShirtDiagnostics(TEXT("EditorStabilize"), true);
			EnsureEditorShirtDiagnosticTicker();
		}

		return;
	}

	if (!ShouldManageClothingLeaderPose())
	{
		return;
	}

	if (UWorld* MutableWorld = GetWorld())
	{
		MutableWorld->GetTimerManager().SetTimerForNextTick(
			FTimerDelegate::CreateUObject(this, &UGodfreyPerformerAnimationBridgeComponent::DeferredClothingStabilize));
	}
#endif
}

void UGodfreyPerformerAnimationBridgeComponent::EnsureMetaHumanCopyPoseBodySource()
{
	if (!HasMetaHumanGarmentPostProcessMesh())
	{
		return;
	}

	if (bAutoResolveMetaHumanBodyMesh && ShouldAutoResolveBodyMesh())
	{
		ResolveTargetBodyMesh();
	}

	USkeletalMeshComponent* Body = TargetSkeletalMesh;
	if (!IsDriveableSkeletalMesh(Body))
	{
		return;
	}

	Body->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	Body->bEnableUpdateRateOptimizations = false;
	Body->bEnableAnimation = true;
	Body->SetDisablePostProcessBlueprint(false);
	ApplyMetaHumanCopyPoseBodyVisibility(Body, IsEditorViewportWorld(), bKeepBodyMeshVisible);

#if WITH_EDITOR
	Body->SetUpdateAnimationInEditor(true);
#endif

	Body->TickAnimation(0.f, false);
	ForceSkeletalMeshPoseRefresh(Body);

	EnsureMetaHumanFaceVisible();
	ApplyMetaHumanClothingTickPrerequisites(Body);

	for (const FName FollowerName : ClothingFollowerMeshNames)
	{
		USkeletalMeshComponent* Follower = FindFollowerMeshByComponentName(FollowerName);
		if (!IsValid(Follower) || !HasRenderableSkeletalMeshAsset(Follower))
		{
			continue;
		}

		Follower->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		Follower->bEnableUpdateRateOptimizations = false;
		Follower->SetHiddenInGame(false, true);

#if WITH_EDITOR
		Follower->SetUpdateAnimationInEditor(true);
#endif

		if (GarmentMeshHasPostProcessAnim(Follower))
		{
			ClearPostProcessGarmentLeaderPose(Follower);
			EnsureMetaHumanCopyPoseGarmentAttachedToBody(Body, Follower);
			WireClothingPostProcessCopyPoseSource(Body, Follower, false);
		}
		else if (Follower->LeaderPoseComponent.Get() != Body)
		{
			Follower->SetLeaderPoseComponent(Body, true, true);
			Follower->UpdateFollowerComponent();
		}
	}

	Body->RefreshFollowerComponents();

	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("GodfreyPerformerBridge: MetaHuman CopyPose body source ready (Body+all garments AlwaysTick; leader pose preserved on Legs/Feet)."));
}

void UGodfreyPerformerAnimationBridgeComponent::MaintainMetaHumanCopyPoseBodySource()
{
	if (!HasMetaHumanGarmentPostProcessMesh() || !IsValid(TargetSkeletalMesh))
	{
		return;
	}

	USkeletalMeshComponent* Body = TargetSkeletalMesh;
	if (Body->VisibilityBasedAnimTickOption != EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones)
	{
		Body->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		Body->bEnableUpdateRateOptimizations = false;
	}

	if (!Body->IsVisible())
	{
		ApplyMetaHumanCopyPoseBodyVisibility(Body, IsEditorViewportWorld(), bKeepBodyMeshVisible);
	}

#if WITH_EDITOR
	if (IsEditorViewportWorld())
	{
		Body->SetUpdateAnimationInEditor(true);
		Body->SetHiddenInGame(false, true);
		RefreshMetaHumanBodyPoseChain(Body);
	}
	else
	{
		Body->SetUpdateAnimationInEditor(true);
		RefreshMetaHumanBodyPoseChain(Body);
	}
#else
	RefreshMetaHumanBodyPoseChain(Body);
#endif

	for (const FName FollowerName : ClothingFollowerMeshNames)
	{
		USkeletalMeshComponent* Follower = FindFollowerMeshByComponentName(FollowerName);
		if (!IsValid(Follower) || !HasRenderableSkeletalMeshAsset(Follower))
		{
			continue;
		}

		if (Follower->VisibilityBasedAnimTickOption != EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones)
		{
			Follower->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
			Follower->bEnableUpdateRateOptimizations = false;
		}

		if (Follower->bHiddenInGame)
		{
			Follower->SetHiddenInGame(false, true);
		}

#if WITH_EDITOR
		if (IsEditorViewportWorld())
		{
			Follower->SetUpdateAnimationInEditor(true);
		}
#endif

		if (GarmentMeshHasPostProcessAnim(Follower))
		{
			ClearPostProcessGarmentLeaderPose(Follower);
			EnsureMetaHumanCopyPoseGarmentAttachedToBody(Body, Follower);
			WireClothingPostProcessCopyPoseSource(Body, Follower, false);
#if WITH_EDITOR
			if (IsEditorViewportWorld() && Follower->GetFName() == FName(TEXT("Torso")))
			{
				Follower->SetDisablePostProcessBlueprint(false);
				if (UAnimInstance* const PostProcessInstance = Follower->GetPostProcessInstance())
				{
					ApplyEditorTorsoCopyPoseOnlyOverrides(PostProcessInstance);
				}
			}
#endif
		}
		else if (Follower->LeaderPoseComponent.Get() != Body)
		{
			Follower->SetLeaderPoseComponent(Body, true, true);
			Follower->UpdateFollowerComponent();
		}
		else
		{
			Follower->UpdateFollowerComponent();
		}
	}

}

#if WITH_EDITOR
void UGodfreyPerformerAnimationBridgeComponent::PinEditorViewportMetaHumanLOD()
{
	if (!IsBridgeActiveForMetaHumanIntervention()
		|| !IsEditorViewportWorld()
		|| !UsesMetaHumanNativeClothingPipeline())
	{
		return;
	}

	if (bAutoResolveMetaHumanBodyMesh && ShouldAutoResolveBodyMesh())
	{
		ResolveTargetBodyMesh();
	}

	if (AActor* const Owner = GetOwner())
	{
		if (ULODSyncComponent* const LODSync = Owner->FindComponentByClass<ULODSyncComponent>())
		{
			LODSync->ForcedLOD = 0;
			LODSync->SetComponentTickEnabled(false);
		}
	}

	USkeletalMeshComponent* Body = TargetSkeletalMesh;
	if (IsDriveableSkeletalMesh(Body))
	{
		Body->SetForcedLOD(0);
		Body->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		Body->SetUpdateAnimationInEditor(true);
	}

	for (const FName FollowerName : ClothingFollowerMeshNames)
	{
		if (USkeletalMeshComponent* Follower = FindFollowerMeshByComponentName(FollowerName))
		{
			Follower->SetForcedLOD(0);
			Follower->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
			Follower->SetUpdateAnimationInEditor(true);
			if (Follower->LeaderPoseComponent.Get() == Body)
			{
				Follower->UpdateFollowerComponent();
			}
		}
	}

	if (IsDriveableSkeletalMesh(Body))
	{
		Body->RefreshFollowerComponents();
	}

	if (!bLoggedEditorViewportLODPin)
	{
		bLoggedEditorViewportLODPin = true;
		UE_LOG(LogGodfreyPerformance, Log,
			TEXT("GodfreyPerformerBridge: editor viewport LOD pinned (LODSync forced=0, garment meshes LOD 0; garment bridge off)."));
	}
}

void UGodfreyPerformerAnimationBridgeComponent::RestoreEditorTorsoCopyPoseGarment(
	USkeletalMeshComponent* Body, USkeletalMeshComponent* Torso)
{
	if (!IsEditorViewportWorld() || !IsValid(Body) || !IsValid(Torso))
	{
		return;
	}

	Torso->SetDisablePostProcessBlueprint(false);
	Torso->SetForcedLOD(0);
	ClearPostProcessGarmentLeaderPose(Torso);
	EnsureMetaHumanCopyPoseGarmentAttachedToBody(Body, Torso);
	WireClothingPostProcessCopyPoseSource(Body, Torso, false);
	if (UAnimInstance* const PostProcessInstance = Torso->GetPostProcessInstance())
	{
		ApplyEditorTorsoCopyPoseOnlyOverrides(PostProcessInstance);
	}
	Torso->UpdateFollowerComponent();
	ForceSkeletalMeshPoseRefresh(Torso);
}

void UGodfreyPerformerAnimationBridgeComponent::RestoreEditorMetaHumanViewportDefaults()
{
	if (!IsEditorViewportWorld())
	{
		return;
	}

	if (AActor* const Owner = GetOwner())
	{
		if (ULODSyncComponent* const LODSync = Owner->FindComponentByClass<ULODSyncComponent>())
		{
			LODSync->ForcedLOD = -1;
			if (!LODSync->IsComponentTickEnabled())
			{
				LODSync->SetComponentTickEnabled(true);
				UE_LOG(LogGodfreyPerformance, Log,
					TEXT("GodfreyPerformerBridge: editor LODSync restored (auto LOD, tick re-enabled)."));
			}
		}
	}

	if (!IsValid(TargetSkeletalMesh))
	{
		return;
	}

	USkeletalMeshComponent* const Body = TargetSkeletalMesh;
	Body->SetForcedLOD(0);

	USkeletalMeshComponent* const Torso = FindFollowerMeshByComponentName(FName(TEXT("Torso")));
	if (IsValid(Torso))
	{
		RestoreEditorTorsoCopyPoseGarment(Body, Torso);
	}
}

void UGodfreyPerformerAnimationBridgeComponent::ReapplyEditorGarmentPreviewSettings()
{
	RestoreEditorMetaHumanViewportDefaults();
}

void UGodfreyPerformerAnimationBridgeComponent::ApplyEditorTorsoCopyPoseOnlyOverrides(
	UAnimInstance* PostProcessInstance) const
{
	if (!PostProcessInstance)
	{
		return;
	}

	static constexpr int32 NeverActiveLODThreshold = 999;
	MetaHumanComponentHelpers::ConnectVariable<FBoolProperty, bool>(
		PostProcessInstance, TEXT("Enable Control Rig"), false);
	MetaHumanComponentHelpers::ConnectVariable<FBoolProperty, bool>(
		PostProcessInstance, TEXT("Enable Rigid Body Simulation"), false);
	MetaHumanComponentHelpers::ConnectVariable<FIntProperty, int32>(
		PostProcessInstance, TEXT("Control Rig LOD Threshold"), NeverActiveLODThreshold);
	MetaHumanComponentHelpers::ConnectVariable<FIntProperty, int32>(
		PostProcessInstance, TEXT("Rigid Body LOD Threshold"), NeverActiveLODThreshold);

	static bool bLoggedEditorTorsoSimOff = false;
	if (!bLoggedEditorTorsoSimOff)
	{
		bLoggedEditorTorsoSimOff = true;
		UE_LOG(LogGodfreyPerformance, Log,
			TEXT("GodfreyPerformerBridge: editor Torso CopyPose-only (CR/RB off, LOD thresholds pinned)."));
	}
}

void UGodfreyPerformerAnimationBridgeComponent::PinClothingToSkinnedPose()
{
	if (!bPinClothingToSkinnedPose)
	{
		return;
	}

	AActor* const Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	++ClothingSkinnedPosePinPassCount;
	if (ClothingSkinnedPosePinPassCount >= 8)
	{
		if (UWorld* const World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(ClothingSkinnedPosePinTimerHandle);
		}
	}

	int32 SkelPinned = 0;
	int32 ChaosPinned = 0;
	int32 PpPinned = 0;

	TArray<UActorComponent*> Components;
	Owner->GetComponents(Components);
	for (UActorComponent* const Comp : Components)
	{
		if (!IsValid(Comp))
		{
			continue;
		}

		const FString CompName = Comp->GetName();
		const FString LowerName = CompName.ToLower();
		if (LowerName.Contains(TEXT("face"))
			|| LowerName.Contains(TEXT("hair"))
			|| LowerName.Contains(TEXT("groom"))
			|| LowerName.Contains(TEXT("beard"))
			|| LowerName.Contains(TEXT("brow"))
			|| LowerName.Contains(TEXT("lash"))
			|| LowerName.Contains(TEXT("lodsync")))
		{
			continue;
		}

		if (USkeletalMeshComponent* const Skel = Cast<USkeletalMeshComponent>(Comp))
		{
			if (!Skel->bDisableClothSimulation
				|| Skel->ClothBlendWeight > KINDA_SMALL_NUMBER
				|| Skel->GetAllowClothActors())
			{
				Skel->bDisableClothSimulation = true;
				Skel->ClothBlendWeight = 0.f;
				Skel->SetClothMaxDistanceScale(0.f);
				Skel->SetAllowClothActors(false);
				++SkelPinned;
			}

			if (UAnimInstance* const PP = Skel->GetPostProcessInstance())
			{
				static constexpr int32 NeverActiveLODThreshold = 999;
				MetaHumanComponentHelpers::ConnectVariable<FBoolProperty, bool>(
					PP, TEXT("Enable Rigid Body Simulation"), false);
				MetaHumanComponentHelpers::ConnectVariable<FBoolProperty, bool>(
					PP, TEXT("Enable Control Rig"), false);
				MetaHumanComponentHelpers::ConnectVariable<FIntProperty, int32>(
					PP, TEXT("Rigid Body LOD Threshold"), NeverActiveLODThreshold);
				MetaHumanComponentHelpers::ConnectVariable<FIntProperty, int32>(
					PP, TEXT("Control Rig LOD Threshold"), NeverActiveLODThreshold);
				++PpPinned;
			}
			continue;
		}

		const FString ClassName = Comp->GetClass()->GetName();
		if (!ClassName.Contains(TEXT("Cloth")))
		{
			continue;
		}

		bool bChanged = false;
		if (FBoolProperty* const EnableSim = FindFProperty<FBoolProperty>(Comp->GetClass(), TEXT("bEnableSimulation")))
		{
			if (EnableSim->GetPropertyValue_InContainer(Comp))
			{
				EnableSim->SetPropertyValue_InContainer(Comp, false);
				bChanged = true;
			}
		}
		if (FBoolProperty* const EnableSimNoPrefix = FindFProperty<FBoolProperty>(Comp->GetClass(), TEXT("EnableSimulation")))
		{
			if (EnableSimNoPrefix->GetPropertyValue_InContainer(Comp))
			{
				EnableSimNoPrefix->SetPropertyValue_InContainer(Comp, false);
				bChanged = true;
			}
		}
		if (FFloatProperty* const Blend = FindFProperty<FFloatProperty>(Comp->GetClass(), TEXT("BlendWeight")))
		{
			if (Blend->GetPropertyValue_InContainer(Comp) > KINDA_SMALL_NUMBER)
			{
				Blend->SetPropertyValue_InContainer(Comp, 0.f);
				bChanged = true;
			}
		}
		if (UFunction* const ResetFn = Comp->FindFunction(FName(TEXT("ForceNextUpdateTeleportAndReset"))))
		{
			Comp->ProcessEvent(ResetFn, nullptr);
		}
		if (bChanged)
		{
			++ChaosPinned;
		}
	}

	if (SkelPinned > 0 || ChaosPinned > 0)
	{
		UE_LOG(LogGodfreyPerformance, Log,
			TEXT("GodfreyPerformerBridge: pinned clothing to skinned pose (skel=%d chaos=%d pp=%d pass=%d)."),
			SkelPinned, ChaosPinned, PpPinned, ClothingSkinnedPosePinPassCount);
	}
}

void UGodfreyPerformerAnimationBridgeComponent::ScheduleClothingSkinnedPosePin()
{
	if (!bPinClothingToSkinnedPose)
	{
		return;
	}

	UWorld* const World = GetWorld();
	if (!World)
	{
		return;
	}

	ClothingSkinnedPosePinPassCount = 0;
	World->GetTimerManager().SetTimer(
		ClothingSkinnedPosePinTimerHandle,
		FTimerDelegate::CreateUObject(this, &UGodfreyPerformerAnimationBridgeComponent::PinClothingToSkinnedPose),
		0.25f,
		true);
	PinClothingToSkinnedPose();
}

void UGodfreyPerformerAnimationBridgeComponent::StabilizeEditorTorsoOnViewportChange()
{
	if (!ShouldManageMetaHumanGarmentsAtRuntime()
		|| !IsEditorViewportWorld()
		|| !HasMetaHumanGarmentPostProcessMesh())
	{
		return;
	}

	USkeletalMeshComponent* const Body = TargetSkeletalMesh;
	USkeletalMeshComponent* const Torso = FindFollowerMeshByComponentName(FName(TEXT("Torso")));
	if (!IsDriveableSkeletalMesh(Body) || !IsDriveableSkeletalMesh(Torso))
	{
		return;
	}

	RestoreEditorTorsoCopyPoseGarment(Body, Torso);
	RefreshMetaHumanBodyPoseChain(Body);

	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("GodfreyPerformerBridge: editor zoom stabilize on Torso (CopyPose, CR/RB off)."));
}
#endif

void UGodfreyPerformerAnimationBridgeComponent::MaybeLogMetaHumanShirtDiagnostics(
	const TCHAR* TriggerReason, const bool bForce)
{
	if (!bLogMetaHumanShirtDiagnostics)
	{
		return;
	}

	USkeletalMeshComponent* const Body = TargetSkeletalMesh;
	USkeletalMeshComponent* const Torso = FindFollowerMeshByComponentName(FName(TEXT("Torso")));
	if (!IsValid(Body) || !IsValid(Torso))
	{
		return;
	}

	const int32 BodyTickOpt = static_cast<int32>(Body->VisibilityBasedAnimTickOption);
	const int32 TorsoTickOpt = static_cast<int32>(Torso->VisibilityBasedAnimTickOption);
	const int32 BodyVisible = Body->IsVisible() ? 1 : 0;
	const int32 TorsoVisible = Torso->IsVisible() ? 1 : 0;
	const int32 BodyHiddenInGame = Body->bHiddenInGame ? 1 : 0;

	bool bStateChanged = BodyTickOpt != LastLoggedBodyTickOpt
		|| TorsoTickOpt != LastLoggedTorsoTickOpt
		|| BodyVisible != LastLoggedBodyVisible
		|| TorsoVisible != LastLoggedTorsoVisible
		|| BodyHiddenInGame != LastLoggedBodyHiddenInGame;

	const int32 BodyLOD = Body->GetPredictedLODLevel();
	const int32 TorsoLOD = Torso->GetPredictedLODLevel();
	bStateChanged = bStateChanged
		|| BodyLOD != LastLoggedBodyLOD
		|| TorsoLOD != LastLoggedTorsoLOD;

	bool bZoomChanged = false;
#if WITH_EDITOR
	if (IsEditorViewportWorld())
	{
		const FEditorViewportWatch Watch = GetEditorViewportWatch(Torso);
		if (Watch.ViewDistance > 0.f)
		{
			if (LastEditorViewDistanceToTorso > 0.f)
			{
				const float AbsDelta = FMath::Abs(Watch.ViewDistance - LastEditorViewDistanceToTorso);
				const float RelativeChange = AbsDelta / FMath::Max(LastEditorViewDistanceToTorso, 1.f);
				bZoomChanged = RelativeChange >= 0.03f || AbsDelta >= 10.f;
			}
			if (LastEditorViewFOV > 0.f && Watch.ViewFOV > 0.f)
			{
				bZoomChanged = bZoomChanged || FMath::Abs(Watch.ViewFOV - LastEditorViewFOV) >= 0.25f;
			}
		}
	}
#endif

	FVector BodyPelvis = FVector::ZeroVector;
	FVector TorsoPelvis = FVector::ZeroVector;
	const bool bHasBodyPelvis = GetBoneWorldLocationSafe(Body, FName(TEXT("pelvis")), BodyPelvis);
	const bool bHasTorsoPelvis = GetBoneWorldLocationSafe(Torso, FName(TEXT("pelvis")), TorsoPelvis);
	const float PelvisDeltaCm = (bHasBodyPelvis && bHasTorsoPelvis) ? FVector::Dist(BodyPelvis, TorsoPelvis) : -1.f;

	const bool bExplosionSuspect = PelvisDeltaCm >= ShirtExplosionPelvisDeltaThresholdCm;
	const bool bPelvisDeltaJump = LastLoggedPelvisDeltaCm >= 0.f && PelvisDeltaCm >= 0.f
		&& FMath::Abs(PelvisDeltaCm - LastLoggedPelvisDeltaCm) >= 25.f;

	const UWorld* const World = GetWorld();
	const double NowSeconds = World ? static_cast<double>(World->GetTimeSeconds()) : 0.0;
	const bool bIntervalElapsed = (NowSeconds - LastShirtDiagnosticLogTimeSeconds) >= static_cast<double>(ShirtDiagnosticMinLogInterval);
	const bool bMeaningfulChange = bStateChanged || bZoomChanged || bExplosionSuspect || bPelvisDeltaJump;

#if WITH_EDITOR
	if (bZoomChanged && IsEditorViewportWorld())
	{
		StabilizeEditorTorsoOnViewportChange();
	}
#endif

	if (!bForce && !bMeaningfulChange && !bIntervalElapsed)
	{
#if WITH_EDITOR
		if (IsEditorViewportWorld())
		{
			const FEditorViewportWatch Watch = GetEditorViewportWatch(Torso);
			if (Watch.ViewDistance > 0.f)
			{
				LastEditorViewDistanceToTorso = Watch.ViewDistance;
			}
			if (Watch.ViewFOV > 0.f)
			{
				LastEditorViewFOV = Watch.ViewFOV;
			}
		}
#endif
		return;
	}

	if (!bForce && !bMeaningfulChange && bIntervalElapsed && TriggerReason
		&& (FCString::Strcmp(TriggerReason, TEXT("MaintainCopyPose")) == 0
			|| FCString::Strcmp(TriggerReason, TEXT("EditorTicker")) == 0))
	{
#if WITH_EDITOR
		if (IsEditorViewportWorld())
		{
			const FEditorViewportWatch Watch = GetEditorViewportWatch(Torso);
			if (Watch.ViewDistance > 0.f)
			{
				LastEditorViewDistanceToTorso = Watch.ViewDistance;
			}
			if (Watch.ViewFOV > 0.f)
			{
				LastEditorViewFOV = Watch.ViewFOV;
			}
		}
#endif
		return;
	}

	const TCHAR* EffectiveReason = TriggerReason;
	if (bExplosionSuspect)
	{
		EffectiveReason = TEXT("EXPLOSION_SUSPECT");
	}
	else if (bZoomChanged)
	{
		EffectiveReason = TEXT("EDITOR_ZOOM");
	}
	else if (bStateChanged)
	{
		EffectiveReason = TEXT("STATE_CHANGE");
	}

#if WITH_EDITOR
	int32 LogBodyLOD = BodyLOD;
	int32 LogTorsoLOD = TorsoLOD;
	const int32 bLodMismatch = (LogTorsoLOD != LogBodyLOD) ? 1 : 0;
	if (IsEditorViewportWorld() && bLodMismatch)
	{
		RestoreEditorTorsoCopyPoseGarment(Body, Torso);
		LogBodyLOD = Body->GetPredictedLODLevel();
		LogTorsoLOD = Torso->GetPredictedLODLevel();
	}
	const ILODSyncInterface* const TorsoLODInterface = Cast<ILODSyncInterface>(Torso);
	const int32 TorsoStreamLOD = TorsoLODInterface ? TorsoLODInterface->GetForceStreamedLOD() : INDEX_NONE;
	const int32 TorsoBestLOD = TorsoLODInterface ? TorsoLODInterface->GetBestAvailableLOD() : INDEX_NONE;
	const int32 bLeaderBody = (Torso->LeaderPoseComponent.Get() == Body) ? 1 : 0;
	const int32 bPPOff = Torso->GetDisablePostProcessBlueprint() ? 1 : 0;
#else
	const int32 LogBodyLOD = BodyLOD;
	const int32 LogTorsoLOD = TorsoLOD;
	const int32 bLodMismatch = 0;
	const int32 TorsoStreamLOD = INDEX_NONE;
	const int32 TorsoBestLOD = INDEX_NONE;
	const int32 bLeaderBody = 0;
	const int32 bPPOff = 0;
#endif

	LastLoggedBodyTickOpt = BodyTickOpt;
	LastLoggedTorsoTickOpt = TorsoTickOpt;
	LastLoggedBodyVisible = BodyVisible;
	LastLoggedTorsoVisible = TorsoVisible;
	LastLoggedBodyHiddenInGame = BodyHiddenInGame;
	LastLoggedBodyLOD = BodyLOD;
	LastLoggedTorsoLOD = TorsoLOD;
	LastLoggedPelvisDeltaCm = PelvisDeltaCm;
	LastShirtDiagnosticLogTimeSeconds = NowSeconds;

#if WITH_EDITOR
	if (IsEditorViewportWorld())
	{
		const FEditorViewportWatch Watch = GetEditorViewportWatch(Torso);
		if (Watch.ViewDistance > 0.f)
		{
			LastEditorViewDistanceToTorso = Watch.ViewDistance;
		}
		if (Watch.ViewFOV > 0.f)
		{
			LastEditorViewFOV = Watch.ViewFOV;
		}
	}
#endif

	if (bExplosionSuspect || bPelvisDeltaJump)
	{
		UE_LOG(LogGodfreyPerformance, Warning,
			TEXT("ShirtDiag [%s] trigger=%s pelvis=%.1fcm maxBone=%.1fcm boundsR=%.0f bodyLOD=%d torsoLOD=%d forcedLOD=%d/%d CR=%d RB=%d zoom=%d state=%d viewDist=%.0f strm=%d best=%d lp=%d pp=%d lodMis=%d"),
			GetOwner() ? *GetOwner()->GetName() : TEXT("(no owner)"),
			EffectiveReason,
			PelvisDeltaCm,
			ComputeMaxWatchBoneDeltaCm(Body, Torso),
			Torso->Bounds.SphereRadius,
			LogBodyLOD,
			LogTorsoLOD,
			Body->GetForcedLOD(),
			Torso->GetForcedLOD(),
			ReadTorsoPostProcessBoolAsInt(Torso, TEXT("Enable Control Rig")),
			ReadTorsoPostProcessBoolAsInt(Torso, TEXT("Enable Rigid Body Simulation")),
			bZoomChanged ? 1 : 0,
			bStateChanged ? 1 : 0,
			LastEditorViewDistanceToTorso,
			TorsoStreamLOD,
			TorsoBestLOD,
			bLeaderBody,
			bPPOff,
			bLodMismatch);
	}
	else if (bZoomChanged || bStateChanged || bForce || bLodMismatch)
	{
		if (bLodMismatch)
		{
			UE_LOG(LogGodfreyPerformance, Warning,
				TEXT("ShirtDiag [%s] trigger=%s pelvis=%.1fcm maxBone=%.1fcm boundsR=%.0f bodyLOD=%d torsoLOD=%d forcedLOD=%d/%d CR=%d RB=%d zoom=%d state=%d viewDist=%.0f strm=%d best=%d lp=%d pp=%d lodMis=%d"),
				GetOwner() ? *GetOwner()->GetName() : TEXT("(no owner)"),
				EffectiveReason,
				PelvisDeltaCm,
				ComputeMaxWatchBoneDeltaCm(Body, Torso),
				Torso->Bounds.SphereRadius,
				LogBodyLOD,
				LogTorsoLOD,
				Body->GetForcedLOD(),
				Torso->GetForcedLOD(),
				ReadTorsoPostProcessBoolAsInt(Torso, TEXT("Enable Control Rig")),
				ReadTorsoPostProcessBoolAsInt(Torso, TEXT("Enable Rigid Body Simulation")),
				bZoomChanged ? 1 : 0,
				bStateChanged ? 1 : 0,
				LastEditorViewDistanceToTorso,
				TorsoStreamLOD,
				TorsoBestLOD,
				bLeaderBody,
				bPPOff,
				bLodMismatch);
		}
		else
		{
			UE_LOG(LogGodfreyPerformance, Log,
				TEXT("ShirtDiag [%s] trigger=%s pelvis=%.1fcm maxBone=%.1fcm boundsR=%.0f bodyLOD=%d torsoLOD=%d forcedLOD=%d/%d CR=%d RB=%d zoom=%d state=%d viewDist=%.0f strm=%d best=%d lp=%d pp=%d lodMis=%d"),
				GetOwner() ? *GetOwner()->GetName() : TEXT("(no owner)"),
				EffectiveReason,
				PelvisDeltaCm,
				ComputeMaxWatchBoneDeltaCm(Body, Torso),
				Torso->Bounds.SphereRadius,
				LogBodyLOD,
				LogTorsoLOD,
				Body->GetForcedLOD(),
				Torso->GetForcedLOD(),
				ReadTorsoPostProcessBoolAsInt(Torso, TEXT("Enable Control Rig")),
				ReadTorsoPostProcessBoolAsInt(Torso, TEXT("Enable Rigid Body Simulation")),
				bZoomChanged ? 1 : 0,
				bStateChanged ? 1 : 0,
				LastEditorViewDistanceToTorso,
				TorsoStreamLOD,
				TorsoBestLOD,
				bLeaderBody,
				bPPOff,
				bLodMismatch);
		}
	}

	if (bExplosionSuspect || bPelvisDeltaJump || bZoomChanged || bStateChanged || bForce)
	{
		LogMetaHumanShirtDiagnosticSnapshot(EffectiveReason);
	}
}

void UGodfreyPerformerAnimationBridgeComponent::LogMetaHumanShirtDiagnosticSnapshot(const TCHAR* TriggerReason) const
{
	USkeletalMeshComponent* const Body = TargetSkeletalMesh;
	USkeletalMeshComponent* const Torso = FindFollowerMeshByComponentName(FName(TEXT("Torso")));
	if (!IsValid(Body) || !IsValid(Torso))
	{
		return;
	}

	const USceneComponent* const TorsoAttachParent = Torso->GetAttachParent();
	const USkinnedMeshComponent* const TorsoLeader = Torso->LeaderPoseComponent.Get();
	UAnimInstance* const BodyPP = Body->GetPostProcessInstance();
	UAnimInstance* const TorsoPP = Torso->GetPostProcessInstance();

	bool bEnableControlRig = false;
	bool bEnableRigidBody = false;
	if (TorsoPP)
	{
		ReadPostProcessBoolProperty(TorsoPP, TEXT("Enable Control Rig"), bEnableControlRig);
		ReadPostProcessBoolProperty(TorsoPP, TEXT("Enable Rigid Body Simulation"), bEnableRigidBody);
	}

	FVector BodyPelvis = FVector::ZeroVector;
	FVector TorsoPelvis = FVector::ZeroVector;
	FVector BodySpine05 = FVector::ZeroVector;
	FVector TorsoSpine05 = FVector::ZeroVector;
	const bool bBodyPelvis = GetBoneWorldLocationSafe(Body, FName(TEXT("pelvis")), BodyPelvis);
	const bool bTorsoPelvis = GetBoneWorldLocationSafe(Torso, FName(TEXT("pelvis")), TorsoPelvis);
	const bool bBodySpine = GetBoneWorldLocationSafe(Body, FName(TEXT("spine_05")), BodySpine05);
	const bool bTorsoSpine = GetBoneWorldLocationSafe(Torso, FName(TEXT("spine_05")), TorsoSpine05);

	const float PelvisDeltaCm = (bBodyPelvis && bTorsoPelvis) ? FVector::Dist(BodyPelvis, TorsoPelvis) : -1.f;
	const float SpineDeltaCm = (bBodySpine && bTorsoSpine) ? FVector::Dist(BodySpine05, TorsoSpine05) : -1.f;

	float MaxWatchBoneDeltaCm = 0.f;
	{
		static const FName WatchBones[] = {
			FName(TEXT("pelvis")),
			FName(TEXT("spine_05")),
			FName(TEXT("clavicle_l")),
			FName(TEXT("clavicle_r")),
		};
		for (const FName BoneName : WatchBones)
		{
			FVector BodyPos = FVector::ZeroVector;
			FVector TorsoPos = FVector::ZeroVector;
			if (GetBoneWorldLocationSafe(Body, BoneName, BodyPos) && GetBoneWorldLocationSafe(Torso, BoneName, TorsoPos))
			{
				MaxWatchBoneDeltaCm = FMath::Max(MaxWatchBoneDeltaCm, FVector::Dist(BodyPos, TorsoPos));
			}
		}
	}

	const float TorsoBoundsRadius = Torso->Bounds.SphereRadius;

#if WITH_EDITOR
	const FEditorViewportWatch ViewWatch = IsEditorViewportWorld() ? GetEditorViewportWatch(Torso) : FEditorViewportWatch{};
	const float ViewDistance = ViewWatch.ViewDistance;
	const float ViewFOV = ViewWatch.ViewFOV;
	const bool bBodyEditorAnim = Body->GetUpdateAnimationInEditor();
	const bool bTorsoEditorAnim = Torso->GetUpdateAnimationInEditor();
#else
	const float ViewDistance = -1.f;
	const float ViewFOV = -1.f;
	const bool bBodyEditorAnim = false;
	const bool bTorsoEditorAnim = false;
#endif

	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("ShirtDiag snapshot reason=%s world=%s editorViewport=%d manageGarmentsRuntime=%d copyPoseGarment=%d"),
		TriggerReason ? TriggerReason : TEXT("(none)"),
		GetWorld() ? *GetWorld()->GetName() : TEXT("(none)"),
		IsEditorViewportWorld() ? 1 : 0,
		bManageMetaHumanGarmentsAtRuntime ? 1 : 0,
		UsesMetaHumanNativeClothingPipeline() ? 1 : 0);

	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("  Body '%s': visible=%d hiddenInGame=%d tickOpt=%s lod=%d registered=%d animInst=%s postProcess=%s editorAnim=%d"),
		*Body->GetName(),
		Body->IsVisible() ? 1 : 0,
		Body->bHiddenInGame ? 1 : 0,
		AnimTickOptionToString(Body->VisibilityBasedAnimTickOption),
		Body->GetPredictedLODLevel(),
		Body->IsRegistered() ? 1 : 0,
		Body->GetAnimInstance() ? *Body->GetAnimInstance()->GetClass()->GetName() : TEXT("(none)"),
		BodyPP ? *BodyPP->GetClass()->GetName() : TEXT("(none)"),
		bBodyEditorAnim ? 1 : 0);

	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("  Torso '%s': visible=%d hiddenInGame=%d tickOpt=%s lod=%d registered=%d attach='%s' leader='%s' postProcess=%s editorAnim=%d"),
		*Torso->GetName(),
		Torso->IsVisible() ? 1 : 0,
		Torso->bHiddenInGame ? 1 : 0,
		AnimTickOptionToString(Torso->VisibilityBasedAnimTickOption),
		Torso->GetPredictedLODLevel(),
		Torso->IsRegistered() ? 1 : 0,
		TorsoAttachParent ? *TorsoAttachParent->GetName() : TEXT("(none)"),
		TorsoLeader ? *TorsoLeader->GetName() : TEXT("(none)"),
		TorsoPP ? *TorsoPP->GetClass()->GetName() : TEXT("(none)"),
		bTorsoEditorAnim ? 1 : 0);

	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("  Torso PP vars: EnableControlRig=%d EnableRigidBody=%d disablePPBlueprint=%d"),
		bEnableControlRig ? 1 : 0,
		bEnableRigidBody ? 1 : 0,
		Torso->GetDisablePostProcessBlueprint() ? 1 : 0);

	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("  Bones world: bodyPelvis=%s torsoPelvis=%s pelvisDelta=%.1fcm spineDelta=%.1fcm maxWatchBoneDelta=%.1fcm torsoBoundsR=%.0f viewDist=%.0f viewFOV=%.1f"),
		bBodyPelvis ? *BodyPelvis.ToCompactString() : TEXT("(missing)"),
		bTorsoPelvis ? *TorsoPelvis.ToCompactString() : TEXT("(missing)"),
		PelvisDeltaCm,
		SpineDeltaCm,
		MaxWatchBoneDeltaCm,
		TorsoBoundsRadius,
		ViewDistance,
		ViewFOV);

	if (PelvisDeltaCm >= ShirtExplosionPelvisDeltaThresholdCm)
	{
		UE_LOG(LogGodfreyPerformance, Warning,
			TEXT("ShirtDiag EXPLOSION_SUSPECT: Torso pelvis %.1fcm from Body (threshold %.0f) — CopyPose likely stale or sim woke with bad input."),
			PelvisDeltaCm,
			ShirtExplosionPelvisDeltaThresholdCm);
	}
}

void UGodfreyPerformerAnimationBridgeComponent::EnsureMetaHumanBodyTicksForClothingPostProcess()
{
	if (!ShouldManageMetaHumanGarmentsAtRuntime())
	{
		return;
	}

	if (!UsesMetaHumanNativeClothingPipeline())
	{
		return;
	}

	if (bAutoResolveMetaHumanBodyMesh && ShouldAutoResolveBodyMesh())
	{
		ResolveTargetBodyMesh();
	}

	USkeletalMeshComponent* Body = TargetSkeletalMesh;
	if (!IsValid(Body) || !HasRenderableSkeletalMeshAsset(Body))
	{
		return;
	}

	Body->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	Body->bEnableUpdateRateOptimizations = false;
	Body->SetHiddenInGame(bKeepBodyMeshVisible ? false : true, true);
	Body->SetVisibility(true, true);

#if WITH_EDITOR
	Body->SetUpdateAnimationInEditor(true);
#endif

	Body->TickAnimation(0.f, false);
	ForceSkeletalMeshPoseRefresh(Body);

	EnsureMetaHumanFaceVisible();
	ApplyMetaHumanClothingTickPrerequisites(Body);

	for (const FName FollowerName : ClothingFollowerMeshNames)
	{
		USkeletalMeshComponent* Follower = FindFollowerMeshByComponentName(FollowerName);
		if (!IsValid(Follower) || !HasRenderableSkeletalMeshAsset(Follower))
		{
			continue;
		}

		Follower->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		Follower->bEnableUpdateRateOptimizations = false;
		Follower->SetHiddenInGame(false, true);

#if WITH_EDITOR
		Follower->SetUpdateAnimationInEditor(true);
#endif

		if (GarmentMeshHasPostProcessAnim(Follower))
		{
			// MetaHuman owns leader pose + CopyPoseFromMesh graph pins — do not InitAnim or clear leader here.
		}
		else if (Follower->LeaderPoseComponent.Get() != Body)
		{
			Follower->SetLeaderPoseComponent(Body, true, true);
			Follower->UpdateFollowerComponent();
		}
	}

	Body->RefreshFollowerComponents();

	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("GodfreyPerformerBridge: MetaHuman hidden Body set to AlwaysTickPoseAndRefreshBones (feeds garment post-process on zoom/LOD)."));
}

void UGodfreyPerformerAnimationBridgeComponent::MaintainMetaHumanBodyTickForClothing()
{
	if (!ShouldManageMetaHumanGarmentsAtRuntime() || !IsValid(TargetSkeletalMesh))
	{
		return;
	}

	USkeletalMeshComponent* Body = TargetSkeletalMesh;
	if (Body->VisibilityBasedAnimTickOption != EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones)
	{
		Body->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		Body->bEnableUpdateRateOptimizations = false;
	}

#if WITH_EDITOR
	Body->SetUpdateAnimationInEditor(true);
#endif

	Body->RefreshBoneTransforms();

	for (const FName FollowerName : ClothingFollowerMeshNames)
	{
		USkeletalMeshComponent* Follower = FindFollowerMeshByComponentName(FollowerName);
		if (!IsValid(Follower) || !HasRenderableSkeletalMeshAsset(Follower))
		{
			continue;
		}

		if (Follower->VisibilityBasedAnimTickOption != EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones)
		{
			Follower->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
			Follower->bEnableUpdateRateOptimizations = false;
		}

#if WITH_EDITOR
		Follower->SetUpdateAnimationInEditor(true);
#endif

		if (GarmentMeshHasPostProcessAnim(Follower))
		{
			// Preserve MetaHuman post-process CopyPoseFromMesh initialization — tick only.
		}
		else if (Follower->LeaderPoseComponent.Get() != Body)
		{
			Follower->SetLeaderPoseComponent(Body, true, true);
			Follower->UpdateFollowerComponent();
		}
	}

	Body->RefreshFollowerComponents();
}

void UGodfreyPerformerAnimationBridgeComponent::DeferredClothingStabilize()
{
	if (!ShouldManageClothingLeaderPose())
	{
		return;
	}

	if (bAutoResolveMetaHumanBodyMesh && ShouldAutoResolveBodyMesh())
	{
		ResolveTargetBodyMesh();
	}

	if (bDebugHideClothingMeshesAtBeginPlay || bDebugForceBodyMeshVisibleAtBeginPlay)
	{
		ApplyBodyMotionDebugVisibility(bDebugHideClothingMeshesAtBeginPlay, bDebugForceBodyMeshVisibleAtBeginPlay);
	}

	const int32 Wired = WireClothingMeshesToBodyLeaderPose();
	InitAssignedBodyAnimClassOnly();
	StabilizeClothingLeaderPoseMeshes();
	RefreshClothingPoseAfterStabilize();
	MaintainClothingLeaderPose();
	LogSkeletalMeshPropagationReport();

	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("GodfreyPerformerBridge: deferred clothing stabilize complete (%d follower(s); overrides MetaHuman OnlyTickPoseWhenRendered)."),
		Wired);
}

void UGodfreyPerformerAnimationBridgeComponent::RefreshClothingPoseAfterStabilize()
{
	if (!IsValid(TargetSkeletalMesh))
	{
		return;
	}

	TargetSkeletalMesh->TickAnimation(0.f, false);
	TargetSkeletalMesh->RefreshBoneTransforms();
	TargetSkeletalMesh->RefreshFollowerComponents();

	for (const FName FollowerName : ClothingFollowerMeshNames)
	{
		if (USkeletalMeshComponent* Follower = FindFollowerMeshByComponentName(FollowerName))
		{
			Follower->TickAnimation(0.f, false);
			Follower->RefreshBoneTransforms();
		}
	}
}

void UGodfreyPerformerAnimationBridgeComponent::BeginPlay()
{
	Super::BeginPlay();

	LoopedBodySlotMontages.Empty();
	BodySlotRemappedMontages.Empty();
	ActiveSpeakingIdlePlayMontage = nullptr;
	SpeakingIdleMontageCycleSeconds = 0.f;
	SpeakingIdleMontageWallCycleSeconds = 0.f;
	SpeakingIdleCycleStartWorldTime = -1.0;

	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("GodfreyPerformerBridge: BeginPlay preferEyeFixed=%d library='%s'."),
		bPreferEyeFixedLibraryVariants ? 1 : 0,
		*PerformanceLibraryPath.ToString());
	if (bPreferEyeFixedLibraryVariants)
	{
		if (UAnimSequence* Probe = LoadLibrarySequenceByStem(TEXT("SpeakingCalmExplanation_01")))
		{
			UE_LOG(LogGodfreyPerformance, Log,
				TEXT("GodfreyPerformerBridge: EyeFixed probe SpeakingCalmExplanation_01 -> '%s'."),
				*Probe->GetName());
		}
		else
		{
			UE_LOG(LogGodfreyPerformance, Warning,
				TEXT("GodfreyPerformerBridge: EyeFixed probe FAILED for SpeakingCalmExplanation_01 under '%s'."),
				*PerformanceLibraryPath.ToString());
		}
	}

	if (AActor* const Owner = GetOwner())
	{
		CachedExhibitYawDegrees = Owner->GetActorRotation().Yaw;
		bHasCachedExhibitYaw = true;
	}
	bFacingOffsetCalibrated = false;

	if (bAutoResolveMetaHumanBodyMesh && ShouldAutoResolveBodyMesh())
	{
		ResolveTargetBodyMesh();
	}
	CacheMeshVisualYawOffset();

	bMetaHumanClothingRefreshPassesScheduled = false;
	bEditorCopyPoseStabilizeScheduled = false;
	MetaHumanGarmentRegisterWaitFrames = 0;
	MetaHumanClothingRefreshPassCount = 0;
	bLoggedCopyPoseWireForTorso = false;
	LastLoggedBodyTickOpt = -1;
	LastLoggedTorsoTickOpt = -1;
	LastLoggedBodyVisible = -1;
	LastLoggedTorsoVisible = -1;
	LastLoggedBodyHiddenInGame = -1;
	LastEditorViewDistanceToTorso = -1.f;
	LastLoggedPelvisDeltaCm = -1.f;

	if (UsesMetaHumanNativeClothingPipeline())
	{
		if (bAutoResolveMetaHumanBodyMesh && ShouldAutoResolveBodyMesh())
		{
			ResolveTargetBodyMesh();
		}

		UE_LOG(LogGodfreyPerformance, Log,
			TEXT("GodfreyPerformerBridge: MetaHuman on '%s' — montage bridge (copyPoseGarment=%d bManageMetaHumanGarmentsAtRuntime=%d)."),
			GetOwner() ? *GetOwner()->GetName() : TEXT("(no owner)"),
			HasMetaHumanGarmentPostProcessMesh() ? 1 : 0,
			bManageMetaHumanGarmentsAtRuntime ? 1 : 0);

		if (ShouldManageMetaHumanGarmentsAtRuntime())
		{
			// MetaHumanComponentUE::BeginPlay resets garment tick — run after it on subsequent frames.
			ScheduleMetaHumanClothingRefreshPasses();
		}

		LogSkeletalMeshPropagationReport();
	}
	else
	{
		LogSkeletalMeshPropagationReport();
	}

		MaybeLogMetaHumanShirtDiagnostics(TEXT("BeginPlay"), true);

	ScheduleClothingSkinnedPosePin();

	if (bKeepBodyMeshVisible || bDebugForceBodyMeshVisibleAtBeginPlay)
	{
		ApplyBodyMotionDebugVisibility(false, true);
	}

	if (ShouldManageClothingLeaderPose())
	{
		if (UWorld* World = GetWorld())
		{
			// MetaHumanComponentUE::BeginPlay sets clothing meshes to OnlyTickPoseWhenRendered — defer until after it runs.
			World->GetTimerManager().SetTimerForNextTick(
				FTimerDelegate::CreateUObject(this, &UGodfreyPerformerAnimationBridgeComponent::DeferredClothingStabilize));
		}
	}

	if (bAutoAssignPerformanceLibraryDefaults)
	{
		TryAssignPerformanceLibraryDefaults();
	}

	if (bAutoAssignPlaceholderMontages)
	{
		TryAssignPlaceholderMontages();
	}

	TryBindPerformerState();
	EnsureBodyMontagePlaybackReady();
	// Always restore stock Face pipeline first — custom Face PP breaks MetaHuman attach + ACE lipsync.
	RestoreMetaHumanFacePostProcessIfNeeded();
	LogMontageSetupStatus();
	UpdatePerformerTickEnabled();

#if !UE_BUILD_SHIPPING
	{
		const FKey OperatorKey = DebugPerformancePlayKey.IsValid() ? DebugPerformancePlayKey : EKeys::K;
		APlayerController* const PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
		UE_LOG(LogGodfreyPerformance, Log,
			TEXT("GodfreyPerformerBridge: operator capture armed enabled=%d key='%s' (serializedValid=%d) cue='%s' takeRecorder=%d pc='%s' tick=%d."),
			bEnableDebugPerformancePlayKey ? 1 : 0,
			*OperatorKey.ToString(),
			DebugPerformancePlayKey.IsValid() ? 1 : 0,
			*DebugPerformancePlayCueId,
			bCaptureTakeRecorderOnDebugPlay ? 1 : 0,
			PC ? *PC->GetName() : TEXT("(none)"),
			IsComponentTickEnabled() ? 1 : 0);
		if (bEnableDebugPerformancePlayKey && GetWorld() && GetWorld()->IsGameWorld())
		{
			RegisterGodfreyOperatorCaptureSlateInput();
		}
	}
#endif

	// Always start look-to-sea (or idle breathing) after AnimInstance is ready — do not wait for speech.
	if (bDriveExhibitionPresenceMontages)
	{
		bEnableBodyMontages = true;
	}
	ScheduleEnsurePresenceIdleAtBeginPlay();

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimerForNextTick(
			FTimerDelegate::CreateUObject(this, &UGodfreyPerformerAnimationBridgeComponent::RestoreMetaHumanFacePostProcessIfNeeded));
	}
}

bool UGodfreyPerformerAnimationBridgeComponent::ShouldAutoResolveBodyMesh() const
{
	if (!IsValid(TargetSkeletalMesh))
	{
		return true;
	}

	if (!HasRenderableSkeletalMeshAsset(TargetSkeletalMesh))
	{
		return true;
	}

	return !TargetSkeletalMesh->GetName().Equals(BodyMeshNameHint, ESearchCase::IgnoreCase);
}

bool UGodfreyPerformerAnimationBridgeComponent::ResolveTargetBodyMesh()
{
	AActor* const Owner = GetOwner();
	if (!Owner)
	{
		return false;
	}

	const FString BodyHint = BodyMeshNameHint;
	const FString FaceExclude = FaceMeshNameExclude;

	TArray<USkeletalMeshComponent*> Meshes;
	Owner->GetComponents<USkeletalMeshComponent>(Meshes);

	USkeletalMeshComponent* Best = nullptr;
	int32 BestScore = -1;

	for (USkeletalMeshComponent* Mesh : Meshes)
	{
		if (!IsValid(Mesh) || IsExcludedFaceMesh(Mesh, FaceExclude))
		{
			continue;
		}

		const int32 Score = ScoreBodyMeshCandidate(Mesh, BodyHint);
		if (Score > BestScore)
		{
			BestScore = Score;
			Best = Mesh;
		}
	}

	if (!Best)
	{
		UE_LOG(LogGodfreyPerformance, Warning,
			TEXT("GodfreyPerformerBridge: ResolveTargetBodyMesh found no usable body mesh on '%s'."),
			*Owner->GetName());
		return false;
	}

	const bool bChanged = TargetSkeletalMesh != Best;
	TargetSkeletalMesh = Best;
	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("GodfreyPerformerBridge: %s TargetSkeletalMesh -> '%s' (score=%d, skel=%s)."),
		bChanged ? TEXT("resolved") : TEXT("confirmed"),
		*Best->GetName(),
		BestScore,
		*Best->GetSkeletalMeshAsset()->GetName());
	return true;
}

bool UGodfreyPerformerAnimationBridgeComponent::HasClothingFollowerMeshesOnBody() const
{
	if (!bAutoWireClothingLeaderPoseToBody || !IsValid(TargetSkeletalMesh))
	{
		return false;
	}

	for (const FName FollowerName : ClothingFollowerMeshNames)
	{
		USkeletalMeshComponent* Follower = FindFollowerMeshByComponentName(FollowerName);
		if (!IsValid(Follower) || Follower == TargetSkeletalMesh)
		{
			continue;
		}

		if (!HasRenderableSkeletalMeshAsset(Follower))
		{
			continue;
		}

		return true;
	}

	return false;
}

void UGodfreyPerformerAnimationBridgeComponent::InitAssignedBodyAnimClassOnly()
{
	if (!HasRenderableSkeletalMeshAsset(TargetSkeletalMesh))
	{
		return;
	}

	if (HasClothingFollowerMeshesOnBody() && !UsesMetaHumanNativeClothingPipeline())
	{
		// Non-MetaHuman performers only: keep Body in bind pose for leader-follower clothing.
		// MetaHuman actors must keep their assigned Body AnimBP (Live Link / retarget).
		TargetSkeletalMesh->SetAnimInstanceClass(nullptr);
		TargetSkeletalMesh->SetAnimationMode(EAnimationMode::AnimationBlueprint);
		TargetSkeletalMesh->InitAnim(true);
		UE_LOG(LogGodfreyPerformance, Log,
			TEXT("GodfreyPerformerBridge: BeginPlay keeps '%s' in bind/reference pose (no AnimInstance) — clothing leader pose + post-process."),
			*TargetSkeletalMesh->GetName());
		return;
	}

	const TSubclassOf<UAnimInstance> ExistingAnimClass = TargetSkeletalMesh->GetAnimClass();
	if (TargetSkeletalMesh->GetAnimInstance())
	{
		return;
	}

	if (!ExistingAnimClass || ExistingAnimClass == UAnimSingleNodeInstance::StaticClass())
	{
		return;
	}

	TargetSkeletalMesh->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	TargetSkeletalMesh->InitAnim(true);

	if (TargetSkeletalMesh->GetAnimInstance())
	{
		UE_LOG(LogGodfreyPerformance, Log,
			TEXT("GodfreyPerformerBridge: initialized assigned AnimClass '%s' on '%s'."),
			*ExistingAnimClass->GetName(),
			*TargetSkeletalMesh->GetName());
	}
}

void UGodfreyPerformerAnimationBridgeComponent::StabilizeClothingLeaderPoseMeshes()
{
	if (UsesMetaHumanNativeClothingPipeline() || !HasClothingFollowerMeshesOnBody() || !IsValid(TargetSkeletalMesh))
	{
		return;
	}

	auto ConfigureMeshAnimTickStability = [](USkeletalMeshComponent* Mesh)
	{
		if (!IsValid(Mesh))
		{
			return;
		}

		Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		Mesh->bEnableUpdateRateOptimizations = false;
		Mesh->SetDisablePostProcessBlueprint(false);

#if WITH_EDITOR
		Mesh->SetUpdateAnimationInEditor(true);
#endif
	};

	ConfigureMeshAnimTickStability(TargetSkeletalMesh);
	// Hidden in game for the player, but keep the component visible so the leader keeps ticking.
	// External costumes (Victorian) need Body rendered for hands/neck.
	TargetSkeletalMesh->SetHiddenInGame(bKeepBodyMeshVisible ? false : true, true);
	TargetSkeletalMesh->SetVisibility(true, true);

	UClass* const ClothingPostProcessClass = LoadClothingPostProcessAnimClass();

	for (const FName FollowerName : ClothingFollowerMeshNames)
	{
		USkeletalMeshComponent* Follower = FindFollowerMeshByComponentName(FollowerName);
		if (!IsValid(Follower) || !HasRenderableSkeletalMeshAsset(Follower))
		{
			continue;
		}

		const EVisibilityBasedAnimTickOption PreviousTick = Follower->VisibilityBasedAnimTickOption;
		ConfigureMeshAnimTickStability(Follower);

		if (PreviousTick != EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones)
		{
			UE_LOG(LogGodfreyPerformance, Log,
				TEXT("GodfreyPerformerBridge: '%s' anim tick %d -> AlwaysTickPoseAndRefreshBones (MetaHuman override)."),
				*Follower->GetName(),
				static_cast<int32>(PreviousTick));
		}

		if (ClothingPostProcessClass)
		{
			Follower->SetOverridePostProcessAnimBP(ClothingPostProcessClass, true);
			Follower->RefreshBoneTransforms();
			UE_LOG(LogGodfreyPerformance, Log,
				TEXT("GodfreyPerformerBridge: clothing post-process AnimBP on '%s'."),
				*Follower->GetName());
		}
	}

	TargetSkeletalMesh->RefreshBoneTransforms();
	TargetSkeletalMesh->RefreshFollowerComponents();
	TargetSkeletalMesh->MarkRenderTransformDirty();
	TargetSkeletalMesh->MarkRenderStateDirty();

	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("GodfreyPerformerBridge: clothing leader pose stabilized (always tick bones, URO off, Body='%s')."),
		*TargetSkeletalMesh->GetName());
}

void UGodfreyPerformerAnimationBridgeComponent::EnsureMontageAnimInstanceReady()
{
	if (!HasRenderableSkeletalMeshAsset(TargetSkeletalMesh))
	{
		return;
	}

	if (TargetSkeletalMesh->GetAnimInstance())
	{
		return;
	}

	const TSubclassOf<UAnimInstance> ExistingAnimClass = TargetSkeletalMesh->GetAnimClass();
	if (ExistingAnimClass && ExistingAnimClass != UAnimSingleNodeInstance::StaticClass())
	{
		TargetSkeletalMesh->SetAnimationMode(EAnimationMode::AnimationBlueprint);
		TargetSkeletalMesh->InitAnim(true);

		if (TargetSkeletalMesh->GetAnimInstance())
		{
			UE_LOG(LogGodfreyPerformance, Log,
				TEXT("GodfreyPerformerBridge: initialized assigned AnimClass '%s' on '%s'."),
				*ExistingAnimClass->GetName(),
				*TargetSkeletalMesh->GetName());
		}
		return;
	}

	const TSubclassOf<UAnimInstance> BootstrapClass =
		(!UsesMetaHumanNativeClothingPipeline() && HasClothingFollowerMeshesOnBody())
		? UGodfreyBodyAnimInstance::StaticClass()
		: UAnimSingleNodeInstance::StaticClass();

	TargetSkeletalMesh->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	TargetSkeletalMesh->SetAnimInstanceClass(BootstrapClass);
	TargetSkeletalMesh->InitAnim(true);

	if (BootstrapClass == UGodfreyBodyAnimInstance::StaticClass())
	{
		UE_LOG(LogGodfreyPerformance, Log,
			TEXT("GodfreyPerformerBridge: bootstrapped GodfreyBodyAnimInstance on '%s' for montage playback (ref pose when idle; safe for clothing leader pose)."),
			*TargetSkeletalMesh->GetName());
	}
	else
	{
		UE_LOG(LogGodfreyPerformance, Warning,
			TEXT("GodfreyPerformerBridge: bootstrapped AnimSingleNodeInstance on '%s' (assign UGodfreyBodyAnimInstance on Body for upper-body montages)."),
			*TargetSkeletalMesh->GetName());
	}
}

void UGodfreyPerformerAnimationBridgeComponent::EnsureBodyMontagePlaybackReady()
{
	if (!IsValid(TargetSkeletalMesh))
	{
		return;
	}

	const bool bNeedsMontageBody =
		bEnableBodyMontages
		|| bDriveExhibitionPresenceMontages
		|| SpeakingIdleMontage != nullptr
		|| ListeningEnterMontage != nullptr
		|| ThinkingMontage != nullptr
		|| IdleBreathingMontage != nullptr
		|| SeaIdleMontage != nullptr;
	if (!bNeedsMontageBody)
	{
		return;
	}

	// Outfit leader-poses from Body — Body must tick/refresh or garments stay in bind pose.
	TargetSkeletalMesh->VisibilityBasedAnimTickOption =
		EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	TargetSkeletalMesh->bEnableUpdateRateOptimizations = false;
	TargetSkeletalMesh->SetDisablePostProcessBlueprint(false);
	if (UAnimInstance* AnimInst = TargetSkeletalMesh->GetAnimInstance())
	{
		if (!bTravelRootMotionActive && AnimInst->RootMotionMode != ERootMotionMode::IgnoreRootMotion)
		{
			AnimInst->RootMotionMode = ERootMotionMode::IgnoreRootMotion;
			UE_LOG(LogGodfreyPerformance, Log,
				TEXT("GodfreyPerformerBridge: RootMotionMode forced to IgnoreRootMotion on '%s' to prevent orientation drift."),
				*TargetSkeletalMesh->GetName());
		}
	}

#if WITH_EDITOR
	TargetSkeletalMesh->SetUpdateAnimationInEditor(true);
#endif

	// Combined outfit meshes (e.g. MHC CaptainGodfrey SkeletalMesh) leader-pose from Body — keep them ticking.
	if (AActor* const Owner = GetOwner())
	{
		TArray<USkeletalMeshComponent*> Meshes;
		Owner->GetComponents<USkeletalMeshComponent>(Meshes);
		for (USkeletalMeshComponent* Mesh : Meshes)
		{
			if (!IsValid(Mesh) || Mesh == TargetSkeletalMesh || !HasRenderableSkeletalMeshAsset(Mesh))
			{
				continue;
			}
			if (Mesh->LeaderPoseComponent.Get() != TargetSkeletalMesh)
			{
				continue;
			}
			Mesh->VisibilityBasedAnimTickOption =
				EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
			Mesh->bEnableUpdateRateOptimizations = false;
#if WITH_EDITOR
			Mesh->SetUpdateAnimationInEditor(true);
#endif
		}
	}
}

FName UGodfreyPerformerAnimationBridgeComponent::GetInPlaceOverlaySlotName() const
{
	return UGodfreyBodyAnimInstance::UpperBodyMontageSlotName;
}

bool UGodfreyPerformerAnimationBridgeComponent::IsFullBodyOverrideContext(const TCHAR* ContextLabel) const
{
	if (!ContextLabel || !*ContextLabel)
	{
		return false;
	}
	const FString C(ContextLabel);
	if (C.Equals(TEXT("SeaIdle"), ESearchCase::IgnoreCase)
		|| C.Equals(TEXT("EngageTurn"), ESearchCase::IgnoreCase)
		|| C.Equals(TEXT("FarewellWave"), ESearchCase::IgnoreCase)
		|| C.Equals(TEXT("BackToSea"), ESearchCase::IgnoreCase)
		|| C.Equals(TEXT("ReturnToIdle"), ESearchCase::IgnoreCase)
		|| C.Equals(TEXT("IdleBreath"), ESearchCase::IgnoreCase)
		|| C.Equals(TEXT("PlantedStance"), ESearchCase::IgnoreCase)
		|| C.Equals(TEXT("NamedActionTravel"), ESearchCase::IgnoreCase))
	{
		return true;
	}
	// Operator capture films the authored full-body take.
	if (bOperatorPerformanceHold && C.Equals(TEXT("NamedAction"), ESearchCase::IgnoreCase))
	{
		return true;
	}
	return false;
}

bool UGodfreyPerformerAnimationBridgeComponent::ShouldApplyRootMotionForAction(const FString& CueId) const
{
	const FString Stem = NormalizePerformanceCueId(CueId);
	if (Stem.IsEmpty())
	{
		return false;
	}

	if (PerformanceActionTable)
	{
		static const FString Context(TEXT("GodfreyPerformanceActionRootMotion"));
		for (const FName& RowName : PerformanceActionTable->GetRowNames())
		{
			if (const FGodfreyPerformanceActionRow* Row =
					PerformanceActionTable->FindRow<FGodfreyPerformanceActionRow>(RowName, Context, false))
			{
				const FName RowCue = Row->CueId.IsNone() ? RowName : Row->CueId;
				if (Row->bApplyRootMotion
					&& (RowCue.ToString().Equals(Stem, ESearchCase::IgnoreCase)
						|| RowName.ToString().Equals(Stem, ESearchCase::IgnoreCase)))
				{
					return true;
				}
			}
		}
	}

	const TArray<FString>& Listed = GetDefault<UUnrealPerformerGodfreySettings>()->GodfreyApplyRootMotionActions;
	for (const FString& Entry : Listed)
	{
		if (NormalizePerformanceCueId(Entry).Equals(Stem, ESearchCase::IgnoreCase))
		{
			return true;
		}
	}
	return false;
}

void UGodfreyPerformerAnimationBridgeComponent::ConfigureSequenceRootHandling(UAnimSequence* Sequence,
	const bool bApplyRootMotion)
{
	if (!Sequence)
	{
		return;
	}

	UPackage* const Pkg = Sequence->GetOutermost();
	const bool bWasDirty = Pkg && Pkg->IsDirty();
	Sequence->bEnableRootMotion = true;
	Sequence->bForceRootLock = !bApplyRootMotion;
	Sequence->RootMotionRootLock = ERootMotionRootLock::RefPose;
	if (Pkg && !bWasDirty)
	{
		Pkg->SetDirtyFlag(false);
	}
}

void UGodfreyPerformerAnimationBridgeComponent::SetAnimInstanceIgnoreRootMotion(const bool bIgnore)
{
	if (!IsValid(TargetSkeletalMesh))
	{
		return;
	}
	if (UAnimInstance* AnimInst = TargetSkeletalMesh->GetAnimInstance())
	{
		AnimInst->RootMotionMode = bIgnore
			? ERootMotionMode::IgnoreRootMotion
			: ERootMotionMode::RootMotionFromMontagesOnly;
	}
}

void UGodfreyPerformerAnimationBridgeComponent::StopPlantedStance(const TCHAR* Reason)
{
	if (!ActivePlantedStanceMontage)
	{
		return;
	}
	if (UAnimInstance* AnimInst = ResolveAnimInstance(TEXT("StopPlantedStance")))
	{
		if (AnimInst->Montage_IsActive(ActivePlantedStanceMontage))
		{
			AnimInst->Montage_Stop(GetBodyMontageBlendOut(), ActivePlantedStanceMontage);
		}
	}
	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("GodfreyPerformerBridge: stopped planted stance (%s)."),
		Reason ? Reason : TEXT("(none)"));
	ActivePlantedStanceMontage = nullptr;
}

void UGodfreyPerformerAnimationBridgeComponent::StopTravelRootMotionIfActive(const TCHAR* Reason)
{
	if (!bTravelRootMotionActive && !ActiveTravelMontage)
	{
		return;
	}
	if (UAnimInstance* AnimInst = ResolveAnimInstance(TEXT("StopTravel")))
	{
		if (ActiveTravelMontage && AnimInst->Montage_IsActive(ActiveTravelMontage))
		{
			AnimInst->Montage_Stop(GetBodyMontageBlendOut(), ActiveTravelMontage);
		}
	}
	ActiveTravelMontage = nullptr;
	bTravelRootMotionActive = false;
	SetAnimInstanceIgnoreRootMotion(true);
	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("GodfreyPerformerBridge: stopped travel root motion (%s)."),
		Reason ? Reason : TEXT("(none)"));
}

void UGodfreyPerformerAnimationBridgeComponent::EnsurePlantedStancePlaying()
{
	if (bTravelRootMotionActive || bOperatorPerformanceHold)
	{
		return;
	}

	UAnimInstance* const AnimInst = ResolveAnimInstance(TEXT("PlantedStance"));
	if (!AnimInst)
	{
		return;
	}
	if (ActivePlantedStanceMontage && AnimInst->Montage_IsActive(ActivePlantedStanceMontage))
	{
		return;
	}

	FString StanceStem = GetDefault<UUnrealPerformerGodfreySettings>()->GodfreyPlantedStanceStem;
	if (StanceStem.IsEmpty())
	{
		StanceStem = TEXT("IdleStanding_01");
	}
	UAnimSequence* StanceSeq = PreferEyeFixedSequence(LoadLibrarySequenceByStem(StanceStem));
	if (!StanceSeq)
	{
		StanceSeq = PreferEyeFixedSequence(LoadLibrarySequenceByStem(TEXT("HandsClasped_01")));
	}
	if (!StanceSeq)
	{
		UE_LOG(LogGodfreyPerformance, Warning,
			TEXT("GodfreyPerformerBridge: planted stance sequence '%s' missing — legs may drop to RefPose."),
			*StanceStem);
		return;
	}

	ConfigureSequenceRootHandling(StanceSeq, false);
	if (UGodfreyBodyAnimInstance* const BodyAnim = Cast<UGodfreyBodyAnimInstance>(AnimInst))
	{
		BodyAnim->SetNeutralStanceSequence(StanceSeq);
	}
	UAnimMontage* const StanceMontage = MakeOrGetPlaceholderMontage(
		StanceSeq, TEXT("PlantedStance"), 10000, UGodfreyBodyAnimInstance::DefaultBodyMontageSlotName);
	if (!StanceMontage)
	{
		return;
	}

	ApplyBodyMontageBlendTimes(StanceMontage, EGodfreyIdleBlendProfile::Body);
	const float Played = AnimInst->Montage_Play(
		StanceMontage,
		0.72f,
		EMontagePlayReturnType::MontageLength,
		0.f,
		/*bStopAllMontages=*/false);
	ApplySpeakingMontageSectionLoop(AnimInst, StanceMontage);
	ActivePlantedStanceMontage = StanceMontage;
	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("GodfreyPerformerBridge: planted stance '%s' on DefaultSlot (played=%.2f) — conversation AS overlay UpperBody."),
		*StanceSeq->GetName(), Played);
}

UAnimMontage* UGodfreyPerformerAnimationBridgeComponent::MakeOrGetPlaceholderMontage(UAnimSequence* Sequence,
	const TCHAR* Label, const int32 LoopCount, FName SlotName)
{
	if (!Sequence)
	{
		return nullptr;
	}
	if (SlotName.IsNone())
	{
		SlotName = GetInPlaceOverlaySlotName();
	}

	UAnimMontage* Montage = UAnimMontage::CreateSlotAnimationAsDynamicMontage(
		Sequence,
		SlotName,
		GetBodyMontageBlendIn(),
		GetBodyMontageBlendOut(),
		1.f,
		FMath::Max(1, LoopCount));
	if (!Montage)
	{
		UE_LOG(LogGodfreyPerformance, Warning,
			TEXT("GodfreyPerformerBridge: failed to create placeholder montage from '%s' for %s."),
			*Sequence->GetName(), Label);
		return nullptr;
	}

	ApplyBodyMontageBlendTimes(Montage);

	GeneratedPlaceholderMontages.Add(Montage);
	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("GodfreyPerformerBridge: placeholder montage for %s from sequence '%s' (slot=%s, len=%.2fs, loopCount=%d)."),
		Label, *Sequence->GetName(), *SlotName.ToString(), Montage->GetPlayLength(), LoopCount);
	return Montage;
}

UAnimMontage* UGodfreyPerformerAnimationBridgeComponent::ResolveLoopedBodySlotMontage(UAnimMontage* Montage,
	const TCHAR* ContextLabel)
{
	return ResolveLoopedSlotMontage(Montage, GetInPlaceOverlaySlotName(), ContextLabel);
}

UAnimMontage* UGodfreyPerformerAnimationBridgeComponent::ResolveLoopedSlotMontage(UAnimMontage* Montage,
	const FName SlotName, const TCHAR* ContextLabel)
{
	if (!Montage)
	{
		return nullptr;
	}

	if (TObjectPtr<UAnimMontage>* Cached = LoopedBodySlotMontages.Find(Montage))
	{
		if (*Cached && MontageHasPlayableSlotTrack(*Cached, SlotName))
		{
			return Cached->Get();
		}
	}

	UAnimMontage* const BaseMontage = ResolveMontageForSlot(Montage, SlotName, ContextLabel);
	if (!BaseMontage)
	{
		return nullptr;
	}

	UAnimSequence* SourceSequence = PreferEyeFixedSequence(ExtractPrimarySequenceFromMontage(BaseMontage));
	if (!SourceSequence)
	{
		SourceSequence = PreferEyeFixedSequence(ExtractPrimarySequenceFromMontage(Montage));
	}
	if (!SourceSequence)
	{
		UE_LOG(LogGodfreyPerformance, Warning,
			TEXT("GodfreyPerformerBridge [%s]: cannot build looped montage for '%s' — no AnimSequence; using one-shot."),
			ContextLabel, *Montage->GetName());
		return BaseMontage;
	}

	UAnimMontage* const LoopedMontage = UAnimMontage::CreateSlotAnimationAsDynamicMontage(
		SourceSequence,
		SlotName,
		GetBodyMontageBlendIn(),
		GetBodyMontageBlendOut(),
		1.f,
		GodfreySpeakingIdleSegmentLoopCount);
	if (!LoopedMontage)
	{
		return BaseMontage;
	}

	ApplyBodyMontageBlendTimes(LoopedMontage);
	GeneratedPlaceholderMontages.Add(LoopedMontage);
	LoopedBodySlotMontages.Add(Montage, LoopedMontage);
	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("GodfreyPerformerBridge [%s]: dynamic speaking montage '%s' from sequence '%s' slot=%s (len=%.2fs)."),
		ContextLabel,
		*LoopedMontage->GetName(),
		*SourceSequence->GetName(),
		*SlotName.ToString(),
		LoopedMontage->GetPlayLength());
	return LoopedMontage;
}

UAnimMontage* UGodfreyPerformerAnimationBridgeComponent::ResolveMontageForBodySlot(UAnimMontage* Montage,
	const TCHAR* ContextLabel)
{
	return ResolveMontageForSlot(Montage, UGodfreyBodyAnimInstance::DefaultBodyMontageSlotName, ContextLabel);
}

UAnimMontage* UGodfreyPerformerAnimationBridgeComponent::ResolveMontageForSlot(UAnimMontage* Montage,
	const FName SlotName, const TCHAR* ContextLabel)
{
	if (!Montage)
	{
		return nullptr;
	}

	const FString SlotSummary = DescribeMontageSlotTracks(Montage);
	if (MontageHasPlayableSlotTrack(Montage, SlotName))
	{
		UE_LOG(LogGodfreyPerformance, Log,
			TEXT("GodfreyPerformerBridge [%s]: montage '%s' slot tracks [%s] — %s OK."),
			ContextLabel, *Montage->GetName(), *SlotSummary, *SlotName.ToString());
		return Montage;
	}

	if (!bAutoRemapMontagesToBodySlot)
	{
		UE_LOG(LogGodfreyPerformance, Warning,
			TEXT("GodfreyPerformerBridge [%s]: montage '%s' slot tracks [%s] — missing %s; auto-remap disabled."),
			ContextLabel, *Montage->GetName(), *SlotSummary, *SlotName.ToString());
		return Montage;
	}

	const bool bUpper = SlotName == UGodfreyBodyAnimInstance::UpperBodyMontageSlotName;
	TMap<TObjectPtr<UAnimMontage>, TObjectPtr<UAnimMontage>>& Cache =
		bUpper ? UpperBodyRemappedMontages : BodySlotRemappedMontages;
	if (TObjectPtr<UAnimMontage>* Cached = Cache.Find(Montage))
	{
		if (*Cached)
		{
			return Cached->Get();
		}
	}

	UAnimSequence* SourceSequence = PreferEyeFixedSequence(ExtractPrimarySequenceFromMontage(Montage));
	if (!SourceSequence)
	{
		SourceSequence = LoadAnimSequenceAssetByPath(
			TEXT("/Game/Godfrey/Animation/Retargeted/As_Godfrey_Talking_Anim"));
	}

	if (!SourceSequence)
	{
		UE_LOG(LogGodfreyPerformance, Error,
			TEXT("GodfreyPerformerBridge [%s]: montage '%s' has slot tracks [%s] but no %s and no AnimSequence to rebuild."),
			ContextLabel, *Montage->GetName(), *SlotSummary, *SlotName.ToString());
		return Montage;
	}

	UAnimMontage* const Remapped = MakeOrGetPlaceholderMontage(SourceSequence, ContextLabel, 1, SlotName);
	if (Remapped)
	{
		Cache.Add(Montage, Remapped);
		UE_LOG(LogGodfreyPerformance, Log,
			TEXT("GodfreyPerformerBridge [%s]: rebuilt '%s' -> dynamic montage '%s' on %s from sequence '%s' (original slots: [%s])."),
			ContextLabel,
			*Montage->GetName(),
			*Remapped->GetName(),
			*SlotName.ToString(),
			*SourceSequence->GetName(),
			*SlotSummary);
	}
	return Remapped ? Remapped : Montage;
}

UAnimMontage* UGodfreyPerformerAnimationBridgeComponent::ResolvePlaceholderMontageAsset()
{
	if (PlaceholderMontageOverride)
	{
		return PlaceholderMontageOverride;
	}

	if (!HasRenderableSkeletalMeshAsset(TargetSkeletalMesh))
	{
		return nullptr;
	}

	USkeleton* const BodySkeleton = TargetSkeletalMesh->GetSkeletalMeshAsset()->GetSkeleton();
	if (!BodySkeleton)
	{
		return nullptr;
	}

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
	AssetRegistry.SearchAllAssets(true);

	auto SkeletonMatches = [BodySkeleton](const USkeleton* Candidate) -> bool
	{
		if (!Candidate || !BodySkeleton)
		{
			return false;
		}
#if WITH_EDITOR
		return Candidate == BodySkeleton || BodySkeleton->IsCompatibleForEditor(Candidate);
#else
		return Candidate == BodySkeleton;
#endif
	};

	auto TryAssetsUnderPath = [&](const FName& Path, UAnimMontage*& OutBestMontage, UAnimSequence*& OutBestSequence,
		int32& OutBestMontageScore, int32& OutBestSequenceScore) -> void
	{
		TArray<FAssetData> PathAssets;
		AssetRegistry.GetAssetsByPath(Path, PathAssets, true);
		for (const FAssetData& Data : PathAssets)
		{
			if (Data.AssetClassPath == UAnimMontage::StaticClass()->GetClassPathName())
			{
				if (UAnimMontage* Montage = Cast<UAnimMontage>(Data.GetAsset()))
				{
					if (!SkeletonMatches(Montage->GetSkeleton()))
					{
						continue;
					}
					const int32 Score = bPreferObviousPlaceholderAnimations
						? ScoreAnimAssetNameForObviousTest(Montage->GetName())
						: 0;
					if (!OutBestMontage || Score > OutBestMontageScore)
					{
						OutBestMontage = Montage;
						OutBestMontageScore = Score;
					}
				}
			}
		}
		for (const FAssetData& Data : PathAssets)
		{
			if (Data.AssetClassPath == UAnimSequence::StaticClass()->GetClassPathName())
			{
				if (UAnimSequence* Sequence = Cast<UAnimSequence>(Data.GetAsset()))
				{
					if (!SkeletonMatches(Sequence->GetSkeleton()))
					{
						continue;
					}
					const int32 Score = bPreferObviousPlaceholderAnimations
						? ScoreAnimAssetNameForObviousTest(Sequence->GetName())
						: 0;
					if (!OutBestSequence || Score > OutBestSequenceScore)
					{
						OutBestSequence = Sequence;
						OutBestSequenceScore = Score;
					}
				}
			}
		}
	};

	UAnimMontage* BestMontage = nullptr;
	UAnimSequence* BestSequence = nullptr;
	int32 BestMontageScore = MIN_int32;
	int32 BestSequenceScore = MIN_int32;

	const TArray<FName> SearchPaths = {
		PlaceholderMontageSearchPath,
		FName(TEXT("/Game/Characters")),
		FName(TEXT("/Game/Animation")),
	};

	for (const FName& Path : SearchPaths)
	{
		TryAssetsUnderPath(Path, BestMontage, BestSequence, BestMontageScore, BestSequenceScore);
	}

	if (BestMontage)
	{
		UE_LOG(LogGodfreyPerformance, Log,
			TEXT("GodfreyPerformerBridge: discovered placeholder AnimMontage '%s' (obviousScore=%d)."),
			*BestMontage->GetName(), BestMontageScore);
		return BestMontage;
	}

	if (BestSequence)
	{
		UE_LOG(LogGodfreyPerformance, Log,
			TEXT("GodfreyPerformerBridge: discovered placeholder AnimSequence '%s' (obviousScore=%d)."),
			*BestSequence->GetName(), BestSequenceScore);
		return MakeOrGetPlaceholderMontage(BestSequence, TEXT("discovered sequence"));
	}

	UE_LOG(LogGodfreyPerformance, Warning,
		TEXT("GodfreyPerformerBridge: no compatible placeholder montage/sequence for skeleton '%s'."),
		*BodySkeleton->GetName());
	return nullptr;
}

int32 UGodfreyPerformerAnimationBridgeComponent::ScoreAnimAssetNameForObviousTest(const FString& AssetName) const
{
	const FString Lower = AssetName.ToLower();
	int32 Score = 0;

	auto Boost = [&](const TCHAR* Token, const int32 Points)
	{
		if (Lower.Contains(Token))
		{
			Score += Points;
		}
	};

	Boost(TEXT("wave"), 120);
	Boost(TEXT("greet"), 100);
	Boost(TEXT("gesture"), 90);
	Boost(TEXT("talk"), 80);
	Boost(TEXT("point"), 80);
	Boost(TEXT("punch"), 75);
	Boost(TEXT("hello"), 70);
	Boost(TEXT("walk"), 40);
	Boost(TEXT("idle"), 10);

	Boost(TEXT("calf"), -120);
	Boost(TEXT("ankle"), -100);
	Boost(TEXT("toe"), -90);
	Boost(TEXT("foot"), -80);
	Boost(TEXT("heel"), -70);

	return Score;
}

USkeletalMeshComponent* UGodfreyPerformerAnimationBridgeComponent::FindFollowerMeshByComponentName(const FName MeshName) const
{
	AActor* const Owner = GetOwner();
	if (!Owner || MeshName.IsNone())
	{
		return nullptr;
	}

	TArray<USkeletalMeshComponent*> Meshes;
	Owner->GetComponents<USkeletalMeshComponent>(Meshes);
	for (USkeletalMeshComponent* Mesh : Meshes)
	{
		if (IsValid(Mesh) && Mesh->GetName().Equals(MeshName.ToString(), ESearchCase::IgnoreCase))
		{
			return Mesh;
		}
	}
	return nullptr;
}

void UGodfreyPerformerAnimationBridgeComponent::LogSkeletalMeshPropagationReport() const
{
	AActor* const Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	UE_LOG(LogGodfreyPerformance, Log, TEXT("GodfreyPerformerBridge mesh propagation report for '%s':"), *Owner->GetName());

	TArray<USkeletalMeshComponent*> Meshes;
	Owner->GetComponents<USkeletalMeshComponent>(Meshes);
	for (USkeletalMeshComponent* Mesh : Meshes)
	{
		if (!IsValid(Mesh))
		{
			continue;
		}

		const USkeletalMesh* SkelAsset = Mesh->GetSkeletalMeshAsset();
		const USkinnedMeshComponent* Leader = Mesh->LeaderPoseComponent.Get();
		const FString LeaderName = Leader ? Leader->GetName() : TEXT("(none)");
		const FString SkelName = SkelAsset ? SkelAsset->GetName() : TEXT("(none)");
		const UClass* AnimClass = Mesh->GetAnimClass();
		const FString AnimClassName = AnimClass ? AnimClass->GetName() : TEXT("(none)");
		const UAnimInstance* AnimInst = Mesh->GetAnimInstance();
		const FString AnimInstName = AnimInst ? AnimInst->GetClass()->GetName() : TEXT("(none)");
		const UAnimInstance* PostProcessInst = Mesh->GetPostProcessInstance();
		const FString PostProcessInstName =
			PostProcessInst ? PostProcessInst->GetClass()->GetName() : TEXT("(none)");
		const USceneComponent* AttachParent = Mesh->GetAttachParent();
		const FString AttachParentName = AttachParent ? AttachParent->GetName() : TEXT("(none)");

		UE_LOG(LogGodfreyPerformance, Log,
			TEXT("  mesh='%s' visible=%d hiddenInGame=%d tickOpt=%d skel='%s' animClass='%s' animInst='%s' postProcess='%s' leader='%s' attach='%s'%s"),
			*Mesh->GetName(),
			Mesh->IsVisible(),
			Mesh->bHiddenInGame,
			static_cast<int32>(Mesh->VisibilityBasedAnimTickOption),
			*SkelName,
			*AnimClassName,
			*AnimInstName,
			*PostProcessInstName,
			*LeaderName,
			*AttachParentName,
			(Mesh == TargetSkeletalMesh.Get()) ? TEXT(" [montage target]") : TEXT(""));
	}
}

int32 UGodfreyPerformerAnimationBridgeComponent::WireClothingMeshesToBodyLeaderPose()
{
	if (!HasRenderableSkeletalMeshAsset(TargetSkeletalMesh))
	{
		UE_LOG(LogGodfreyPerformance, Warning, TEXT("GodfreyPerformerBridge: cannot wire leader pose — Body target mesh missing."));
		return 0;
	}

	int32 WiredCount = 0;
	for (const FName FollowerName : ClothingFollowerMeshNames)
	{
		USkeletalMeshComponent* Follower = FindFollowerMeshByComponentName(FollowerName);
		if (!IsValid(Follower) || Follower == TargetSkeletalMesh)
		{
			continue;
		}

		if (!HasRenderableSkeletalMeshAsset(Follower))
		{
			UE_LOG(LogGodfreyPerformance, Verbose, TEXT("GodfreyPerformerBridge: skip leader pose for '%s' (no skeletal mesh asset)."),
				*Follower->GetName());
			continue;
		}

		const USkinnedMeshComponent* ExistingLeader = Follower->LeaderPoseComponent.Get();
		if (ExistingLeader == TargetSkeletalMesh)
		{
			UE_LOG(LogGodfreyPerformance, Log, TEXT("GodfreyPerformerBridge: '%s' already follows Body."), *Follower->GetName());
			++WiredCount;
			continue;
		}

		Follower->SetLeaderPoseComponent(TargetSkeletalMesh, true, true);
		++WiredCount;
		UE_LOG(LogGodfreyPerformance, Log, TEXT("GodfreyPerformerBridge: wired leader pose '%s' -> Body."), *Follower->GetName());
	}

	return WiredCount;
}

void UGodfreyPerformerAnimationBridgeComponent::ApplyBodyMotionDebugVisibility(const bool bHideClothingMeshes,
	const bool bForceBodyMeshVisible)
{
	if (bHideClothingMeshes)
	{
		for (const FName FollowerName : ClothingFollowerMeshNames)
		{
			if (USkeletalMeshComponent* Follower = FindFollowerMeshByComponentName(FollowerName))
			{
				Follower->SetHiddenInGame(true, true);
				UE_LOG(LogGodfreyPerformance, Log, TEXT("GodfreyPerformerBridge debug: hid clothing mesh '%s'."), *Follower->GetName());
			}
		}
	}

	if (bForceBodyMeshVisible && IsValid(TargetSkeletalMesh))
	{
		TargetSkeletalMesh->SetHiddenInGame(false, true);
		TargetSkeletalMesh->SetVisibility(true, true);
		UE_LOG(LogGodfreyPerformance, Log, TEXT("GodfreyPerformerBridge debug: forced Body mesh visible."));
	}
}

bool UGodfreyPerformerAnimationBridgeComponent::TryAssignPlaceholderMontages()
{
	UAnimMontage* const Placeholder = ResolvePlaceholderMontageAsset();
	if (!Placeholder)
	{
		return false;
	}

	auto AssignIfEmpty = [Placeholder](TObjectPtr<UAnimMontage>& Slot, const TCHAR* Label) -> bool
	{
		if (Slot)
		{
			return false;
		}
		Slot = Placeholder;
		UE_LOG(LogGodfreyPerformance, Log, TEXT("GodfreyPerformerBridge: assigned placeholder to %s."), Label);
		return true;
	};

	bool bAny = false;
	bAny |= AssignIfEmpty(SpeakingStartMontage, TEXT("SpeakingStartMontage"));
	bAny |= AssignIfEmpty(SpeakingIdleMontage, TEXT("SpeakingIdleMontage"));
	bAny |= AssignIfEmpty(ReturnToIdleMontage, TEXT("ReturnToIdleMontage"));
	bAny |= AssignIfEmpty(ThinkingMontage, TEXT("ThinkingMontage"));
	bAny |= AssignIfEmpty(ListeningEnterMontage, TEXT("ListeningEnterMontage"));
	return bAny;
}

FString UGodfreyPerformerAnimationBridgeComponent::NormalizePerformanceCueId(const FString& CueId)
{
	FString Stem = CueId.TrimStartAndEnd();
	if (Stem.StartsWith(TEXT("AS_"), ESearchCase::IgnoreCase))
	{
		Stem.RightChopInline(3);
	}
	else if (Stem.StartsWith(TEXT("AM_"), ESearchCase::IgnoreCase))
	{
		Stem.RightChopInline(3);
	}
	StripEyeFixedSuffix(Stem);
	return Stem;
}

void UGodfreyPerformerAnimationBridgeComponent::StripEyeFixedSuffix(FString& InOutStem)
{
	static const TCHAR Suffix[] = TEXT("_EyeFixed");
	if (InOutStem.EndsWith(Suffix, ESearchCase::IgnoreCase))
	{
		InOutStem.LeftChopInline(FCString::Strlen(Suffix));
	}
}

bool UGodfreyPerformerAnimationBridgeComponent::IsNamedActionCueType(const FString& CueType)
{
	const FString T = CueType.TrimStartAndEnd().ToLower();
	return T == TEXT("action") || T == TEXT("performance") || T == TEXT("gesture") || T == TEXT("anim")
		|| T == TEXT("clip");
}

bool UGodfreyPerformerAnimationBridgeComponent::LooksLikeNamedPerformanceId(const FString& Token)
{
	const FString V = Token.TrimStartAndEnd();
	if (V.IsEmpty())
	{
		return false;
	}
	if (V.StartsWith(TEXT("AS_"), ESearchCase::IgnoreCase) || V.StartsWith(TEXT("AM_"), ESearchCase::IgnoreCase))
	{
		return true;
	}
	int32 UnderscoreIndex = INDEX_NONE;
	if (V.FindLastChar(TEXT('_'), UnderscoreIndex) && UnderscoreIndex > 0 && UnderscoreIndex + 1 < V.Len())
	{
		const FString Suffix = V.Mid(UnderscoreIndex + 1);
		if (Suffix.Len() >= 2 && Suffix.IsNumeric())
		{
			return true;
		}
	}
	return false;
}

UAnimSequence* UGodfreyPerformerAnimationBridgeComponent::LoadLibrarySequenceByStem(const FString& AssetStem) const
{
	const FString Normalized = NormalizePerformanceCueId(AssetStem);
	if (Normalized.IsEmpty())
	{
		return nullptr;
	}

	const FString LibraryRoot = PerformanceLibraryPath.ToString();
	if (bPreferEyeFixedLibraryVariants)
	{
		const FString FixedPackage =
			FString::Printf(TEXT("%s/AS_%s_EyeFixed"), *LibraryRoot, *Normalized);
		if (UAnimSequence* Fixed = LoadAnimSequenceAssetByPath(FixedPackage))
		{
			return Fixed;
		}
	}

	const FString Package = FString::Printf(TEXT("%s/AS_%s"), *LibraryRoot, *Normalized);
	return LoadAnimSequenceAssetByPath(Package);
}

UAnimSequence* UGodfreyPerformerAnimationBridgeComponent::PreferEyeFixedSequence(UAnimSequence* InSequence) const
{
	if (!IsValid(InSequence))
	{
		return nullptr;
	}
	if (!bPreferEyeFixedLibraryVariants || InSequence->GetName().Contains(TEXT("_EyeFixed"), ESearchCase::IgnoreCase))
	{
		return InSequence;
	}

	if (UAnimSequence* Preferred = LoadLibrarySequenceByStem(InSequence->GetName()))
	{
		if (Preferred->GetName().Contains(TEXT("_EyeFixed"), ESearchCase::IgnoreCase))
		{
			return Preferred;
		}
	}
	return InSequence;
}

UAnimMontage* UGodfreyPerformerAnimationBridgeComponent::LoadLibraryMontageByStem(const FString& AssetStem) const
{
	const FString Normalized = NormalizePerformanceCueId(AssetStem);
	if (Normalized.IsEmpty())
	{
		return nullptr;
	}

	const FString LibraryRoot = PerformanceLibraryPath.ToString();
	if (bPreferEyeFixedLibraryVariants)
	{
		const FString FixedStem = FString::Printf(TEXT("AM_%s_EyeFixed"), *Normalized);
		const FString FixedPath =
			FString::Printf(TEXT("%s/%s.%s"), *LibraryRoot, *FixedStem, *FixedStem);
		if (UAnimMontage* Fixed = LoadObject<UAnimMontage>(nullptr, *FixedPath))
		{
			return Fixed;
		}
		if (UAnimMontage* FixedSoft = Cast<UAnimMontage>(
				FSoftObjectPath(FixedPath).TryLoad()))
		{
			return FixedSoft;
		}

		// Prefer building a runtime montage from AS_*_EyeFixed over an authored AM that still
		// references the uncorrected source sequence.
		const FString FixedSeqPackage =
			FString::Printf(TEXT("%s/AS_%s_EyeFixed"), *LibraryRoot, *Normalized);
		if (LoadAnimSequenceAssetByPath(FixedSeqPackage) != nullptr)
		{
			return nullptr;
		}
	}

	const FString Stem = FString::Printf(TEXT("AM_%s"), *Normalized);
	const FString Path = FString::Printf(TEXT("%s/%s.%s"), *LibraryRoot, *Stem, *Stem);
	if (UAnimMontage* Authored = LoadObject<UAnimMontage>(nullptr, *Path))
	{
		return Authored;
	}
	return Cast<UAnimMontage>(FSoftObjectPath(Path).TryLoad());
}

UAnimMontage* UGodfreyPerformerAnimationBridgeComponent::RemapMontageToEyeFixedVariant(UAnimMontage* Montage)
{
	if (!bPreferEyeFixedLibraryVariants || !IsValid(Montage))
	{
		return Montage;
	}

	const FString MontageName = Montage->GetName();
	if (MontageName.Contains(TEXT("_EyeFixed"), ESearchCase::IgnoreCase))
	{
		return Montage;
	}

	// Dynamic placeholders are named AnimMontage_### — resolve stem from the embedded sequence.
	FString Stem;
	UAnimSequence* PrimarySeq = ExtractPrimarySequenceFromMontage(Montage);
	if (PrimarySeq)
	{
		const FString SeqName = PrimarySeq->GetName();
		if (SeqName.Contains(TEXT("_EyeFixed"), ESearchCase::IgnoreCase))
		{
			return Montage;
		}
		Stem = NormalizePerformanceCueId(SeqName);
	}
	if (Stem.IsEmpty())
	{
		Stem = NormalizePerformanceCueId(MontageName);
	}
	if (Stem.IsEmpty() || Stem.StartsWith(TEXT("AnimMontage"), ESearchCase::IgnoreCase))
	{
		return Montage;
	}

	if (UAnimMontage* FixedAm = LoadLibraryMontageByStem(Stem))
	{
		if (FixedAm != Montage && FixedAm->GetName().Contains(TEXT("_EyeFixed"), ESearchCase::IgnoreCase))
		{
			UE_LOG(LogGodfreyPerformance, Log,
				TEXT("GodfreyPerformerBridge: remapped montage '%s' -> '%s' (EyeFixed AM)."),
				*MontageName, *FixedAm->GetName());
			return FixedAm;
		}
	}

	if (UAnimSequence* FixedSeq = LoadLibrarySequenceByStem(Stem))
	{
		if (FixedSeq->GetName().Contains(TEXT("_EyeFixed"), ESearchCase::IgnoreCase))
		{
			UAnimMontage* const Built = MakeOrGetPlaceholderMontage(FixedSeq, TEXT("EyeFixed"), 1);
			if (Built)
			{
				UE_LOG(LogGodfreyPerformance, Log,
					TEXT("GodfreyPerformerBridge: remapped montage '%s' (stem '%s') -> '%s' from '%s'."),
					*MontageName, *Stem, *Built->GetName(), *FixedSeq->GetName());
				return Built;
			}
		}
		else
		{
			UE_LOG(LogGodfreyPerformance, Warning,
				TEXT("GodfreyPerformerBridge: EyeFixed missing for stem '%s' (resolved '%s') — using unfixed."),
				*Stem, *FixedSeq->GetName());
		}
	}

	return Montage;
}

bool UGodfreyPerformerAnimationBridgeComponent::TryAssignPerformanceLibraryDefaults()
{
	auto IsLegacyOrEmptySlot = [](const UAnimMontage* Slot) -> bool
	{
		if (!Slot)
		{
			return true;
		}
		const FString PathName = Slot->GetPathName();
		// Phase-8 Mixamo talking placeholder — replace with Performances library defaults.
		return PathName.Contains(TEXT("As_Godfrey_Talking_Anim"), ESearchCase::IgnoreCase)
			|| PathName.Contains(TEXT("/Retargeted/"), ESearchCase::IgnoreCase);
	};

	auto AssignFromSoft = [this, &IsLegacyOrEmptySlot](TObjectPtr<UAnimMontage>& Slot,
		const TSoftObjectPtr<UAnimSequence>& SoftSeq, const TCHAR* Label, const int32 LoopCount) -> bool
	{
		if (Slot && !IsLegacyOrEmptySlot(Slot))
		{
			// Slot already filled (often a dynamic AnimMontage_### from the unfixed AS).
			if (bPreferEyeFixedLibraryVariants)
			{
				UAnimMontage* const Remapped = RemapMontageToEyeFixedVariant(Slot.Get());
				if (Remapped && Remapped != Slot.Get())
				{
					Slot = Remapped;
					UE_LOG(LogGodfreyPerformance, Log,
						TEXT("GodfreyPerformerBridge: upgraded %s slot to EyeFixed montage '%s'."),
						Label, *Remapped->GetName());
					return true;
				}
			}
			return false;
		}

		UAnimSequence* Sequence = nullptr;
		if (SoftSeq.ToSoftObjectPath().IsValid())
		{
			Sequence = LoadLibrarySequenceByStem(SoftSeq.GetAssetName());
		}
		if (!Sequence)
		{
			Sequence = SoftSeq.LoadSynchronous();
			Sequence = PreferEyeFixedSequence(Sequence);
		}
		if (!Sequence)
		{
			UE_LOG(LogGodfreyPerformance, Warning,
				TEXT("GodfreyPerformerBridge: library default sequence missing for %s (%s)."),
				Label, *SoftSeq.ToString());
			return false;
		}

		UE_LOG(LogGodfreyPerformance, Log,
			TEXT("GodfreyPerformerBridge: resolving %s from sequence '%s' (preferEyeFixed=%d)."),
			Label, *Sequence->GetName(), bPreferEyeFixedLibraryVariants ? 1 : 0);

		const FString Stem = NormalizePerformanceCueId(Sequence->GetName());
		if (UAnimMontage* Authored = LoadLibraryMontageByStem(Stem))
		{
			Slot = Authored;
			UE_LOG(LogGodfreyPerformance, Log,
				TEXT("GodfreyPerformerBridge: assigned authored montage '%s' to %s."), *Authored->GetName(), Label);
			return true;
		}

		Slot = MakeOrGetPlaceholderMontage(Sequence, Label, LoopCount);
		if (Slot)
		{
			UE_LOG(LogGodfreyPerformance, Log,
				TEXT("GodfreyPerformerBridge: assigned placeholder for %s from sequence '%s'."),
				Label, *Sequence->GetName());
		}
		return Slot != nullptr;
	};

	bool bAny = false;
	bAny |= AssignFromSoft(ListeningEnterMontage, DefaultListeningSequence, TEXT("ListeningEnter"), 1);
	bAny |= AssignFromSoft(ThinkingMontage, DefaultThinkingSequence, TEXT("Thinking"), 1);
	bAny |= AssignFromSoft(SpeakingStartMontage, DefaultSpeakingStartSequence, TEXT("SpeakingStart"), 1);
	bAny |= AssignFromSoft(SpeakingIdleMontage, DefaultSpeakingIdleSequence, TEXT("SpeakingIdle"),
		GodfreySpeakingIdleSegmentLoopCount);
	bAny |= AssignFromSoft(EmphasisMontage, DefaultEmphasisSequence, TEXT("Emphasis"), 1);
	bAny |= AssignFromSoft(AmusedMontage, DefaultAmusedSequence, TEXT("Amused"), 1);
	bAny |= AssignFromSoft(SeriousMontage, DefaultSeriousSequence, TEXT("Serious"), 1);
	bAny |= AssignFromSoft(ReturnToIdleMontage, DefaultReturnToIdleSequence, TEXT("ReturnToIdle"), 1);
	// Earlier eye-fix workaround parked SeaIdle/IdleBreathing on IdleStanding — rebind to catalog defaults.
	ClearMontageSlotIfStemEquals(IdleBreathingMontage, TEXT("IdleStanding_01"));
	ClearMontageSlotIfStemEquals(SeaIdleMontage, TEXT("IdleStanding_01"));
	bAny |= AssignFromSoft(IdleBreathingMontage, DefaultIdleBreathingSequence, TEXT("IdleBreathing"), 1);
	bAny |= AssignFromSoft(SeaIdleMontage, DefaultSeaIdleSequence, TEXT("SeaIdle"), 1);
	bAny |= AssignFromSoft(EngageTurnMontage, DefaultEngageTurnSequence, TEXT("EngageTurn"), 1);
	bAny |= AssignFromSoft(EngageGreetMontage, DefaultEngageGreetSequence, TEXT("EngageGreet"), 1);
	bAny |= AssignFromSoft(FarewellWaveMontage, DefaultFarewellWaveSequence, TEXT("FarewellWave"), 1);
	bAny |= AssignFromSoft(BackToSeaMontage, DefaultBackToSeaSequence, TEXT("BackToSea"), 1);

	// Legacy default (0.55) stretched speaking-pool clips to ~half speed and looked like a freeze/glitch
	// on long occasion speeches. Bump serialized BP values that still carry the old default.
	if (FMath::IsNearlyEqual(SpeakingMotionIntensity, 0.55f, 0.01f))
	{
		SpeakingMotionIntensity = 0.9f;
		UE_LOG(LogGodfreyPerformance, Log,
			TEXT("GodfreyPerformerBridge: migrated SpeakingMotionIntensity 0.55 -> 0.9 (legacy half-speed speaking body)."));
	}

	if (bAny)
	{
		if (bEnableBodyMontagesWhenLibraryReady && !bEnableBodyMontages)
		{
			bEnableBodyMontages = true;
			UE_LOG(LogGodfreyPerformance, Log,
				TEXT("GodfreyPerformerBridge: enabled bEnableBodyMontages after library defaults assigned."));
		}
		UE_LOG(LogGodfreyPerformance, Log,
			TEXT("GodfreyPerformerBridge: performance library defaults assigned (path=%s, bEnableBodyMontages=%d)."),
			*PerformanceLibraryPath.ToString(), bEnableBodyMontages ? 1 : 0);
	}
	return bAny;
}

void UGodfreyPerformerAnimationBridgeComponent::SuppressSpeakingIdleUntil(const double WorldTimeSeconds)
{
	SuppressSpeakingIdleUntilWorldTime = FMath::Max(SuppressSpeakingIdleUntilWorldTime, WorldTimeSeconds);
}

UAnimMontage* UGodfreyPerformerAnimationBridgeComponent::ResolveNamedActionMontage(const FString& CueId,
	bool& bOutInterruptSpeakingIdle)
{
	bOutInterruptSpeakingIdle = true;
	const FString Stem = NormalizePerformanceCueId(CueId);
	if (Stem.IsEmpty())
	{
		return nullptr;
	}
	const FString EffectiveStem =
		(bOverrideDownwardGazeActions || bStrictCameraSafeActionRemap) ? ResolveGazeSafeActionStem(Stem) : Stem;
	if (!EffectiveStem.Equals(Stem, ESearchCase::IgnoreCase))
	{
		UE_LOG(LogGodfreyPerformance, Log,
			TEXT("GodfreyPerformerBridge: action remap '%s' -> '%s' (gaze-safe override)."),
			*Stem, *EffectiveStem);
	}

	if (PerformanceActionTable)
	{
		static const FString Context(TEXT("GodfreyPerformanceAction"));
		TArray<FName> RowNames = PerformanceActionTable->GetRowNames();
		for (const FName& RowName : RowNames)
		{
			if (const FGodfreyPerformanceActionRow* Row =
					PerformanceActionTable->FindRow<FGodfreyPerformanceActionRow>(RowName, Context, false))
			{
				const FName RowCue = Row->CueId.IsNone() ? RowName : Row->CueId;
				if (!RowCue.ToString().Equals(EffectiveStem, ESearchCase::IgnoreCase)
					&& !RowCue.ToString().Equals(CueId.TrimStartAndEnd(), ESearchCase::IgnoreCase)
					&& !RowName.ToString().Equals(EffectiveStem, ESearchCase::IgnoreCase))
				{
					continue;
				}

				bOutInterruptSpeakingIdle = Row->bInterruptSpeakingIdle;
				if (UAnimMontage* Montage = Row->Montage.LoadSynchronous())
				{
					return RemapMontageToEyeFixedVariant(Montage);
				}
				if (UAnimSequence* Sequence = PreferEyeFixedSequence(Row->Sequence.LoadSynchronous()))
				{
					return MakeOrGetPlaceholderMontage(Sequence, TEXT("NamedActionTable"), 1);
				}
			}
		}
	}

	if (UAnimMontage* Authored = LoadLibraryMontageByStem(EffectiveStem))
	{
		return RemapMontageToEyeFixedVariant(Authored);
	}
	if (UAnimSequence* Sequence = LoadLibrarySequenceByStem(EffectiveStem))
	{
		return MakeOrGetPlaceholderMontage(Sequence, TEXT("NamedAction"), 1);
	}

	return nullptr;
}

bool UGodfreyPerformerAnimationBridgeComponent::PlayNamedPerformanceAction(const FString& CueId,
	const bool bInterruptSpeakingIdle, const bool bIgnorePresenceLock)
{
	LastActingCueType = TEXT("action");
	LastActingCueValue = CueId;

	if (!bIgnorePresenceLock && ShouldSuppressPresenceOwnedBodyCues())
	{
		const int32 PresenceInt = PerformerState
			? static_cast<int32>(PerformerState->GetExhibitionPresence())
			: -1;
		UE_LOG(LogGodfreyAnimation, Log,
			TEXT("[Acting] miss | t=%.3f | SpeechId=(none) | context=NamedAction | cueValue=%s | reason=presence-owns-body | presence=%d"),
			GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0,
			*CueId,
			PresenceInt);
		return false;
	}

	if (bIgnorePresenceLock)
	{
		ClearSeaIdleChainTimer();
		ClearSpeakingIdleChainTimer();
		ClearDialogIdleChainTimer();
	}

	bool bInterrupt = bInterruptSpeakingIdle;
	UAnimMontage* const Montage = ResolveNamedActionMontage(CueId, bInterrupt);
	if (!Montage)
	{
		UE_LOG(LogGodfreyPerformance, Warning,
			TEXT("GodfreyPerformerBridge: named action '%s' not found under '%s' (or PerformanceActionTable)."),
			*CueId, *PerformanceLibraryPath.ToString());
		FString SpeechId = TEXT("(none)");
		if (const UGodfreyDiagnosticsSubsystem* Diag = UGodfreyDiagnosticsSubsystem::Get(this))
		{
			SpeechId = Diag->GetCurrentSpeechId();
			if (SpeechId.IsEmpty())
			{
				SpeechId = TEXT("(none)");
			}
		}
		UE_LOG(LogGodfreyAnimation, Warning,
			TEXT("[Acting] miss | t=%.3f | SpeechId=%s | context=NamedAction | cueValue=%s | reason=not-found"),
			GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0,
			*SpeechId,
			*CueId);
		return false;
	}

	if (bInterrupt && bIsSpeaking)
	{
		if (UAnimInstance* AnimInst = IsValid(TargetSkeletalMesh) ? TargetSkeletalMesh->GetAnimInstance() : nullptr)
		{
			if (ActiveSpeakingIdlePlayMontage && AnimInst->Montage_IsActive(ActiveSpeakingIdlePlayMontage))
			{
				AnimInst->Montage_Stop(GetSpeakingIdleMontageBlendOut(), ActiveSpeakingIdlePlayMontage);
			}
		}
		const float SuppressSeconds = FMath::Max(0.35f, Montage->GetPlayLength() * 0.85f);
		if (const UWorld* World = GetWorld())
		{
			SuppressSpeakingIdleUntil(World->GetTimeSeconds() + SuppressSeconds);
		}
	}

	const bool bTravel = ShouldApplyRootMotionForAction(CueId);
	const TCHAR* const ActionContext = bTravel ? TEXT("NamedActionTravel") : TEXT("NamedAction");
	const bool bPlayed = PlayMontageIfPossible(Montage, ActionContext, 1.f, true, false, false, !bTravel, bTravel);
	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("GodfreyPerformerBridge: named action '%s' -> montage '%s' played=%d travel=%d."),
		*CueId, *Montage->GetName(), bPlayed ? 1 : 0, bTravel ? 1 : 0);
	return bPlayed;
}

bool UGodfreyPerformerAnimationBridgeComponent::PlayOperatorPerformanceClip(const FString& CueId)
{
	const FString EffectiveCue = CueId.TrimStartAndEnd().IsEmpty()
		? DebugPerformancePlayCueId
		: CueId.TrimStartAndEnd();
	if (bOperatorPerformanceHold)
	{
		return false;
	}

	StartOperatorCapture(EffectiveCue);
	return bOperatorPerformanceHold;
}

void UGodfreyPerformerAnimationBridgeComponent::LogMontageSetupStatus() const
{
	auto LogSlot = [](const TCHAR* Label, const UAnimMontage* Montage)
	{
		if (Montage)
		{
			UE_LOG(LogGodfreyPerformance, Log, TEXT("GodfreyPerformerBridge setup: %s = '%s'"), Label, *Montage->GetName());
		}
		else
		{
			UE_LOG(LogGodfreyPerformance, Warning, TEXT("GodfreyPerformerBridge setup: %s is NOT assigned (assign a body montage in Details)."), Label);
		}
	};

	if (IsValid(TargetSkeletalMesh))
	{
		UE_LOG(LogGodfreyPerformance, Log, TEXT("GodfreyPerformerBridge setup: TargetSkeletalMesh = '%s'"),
			*TargetSkeletalMesh->GetName());
	}
	else
	{
		UE_LOG(LogGodfreyPerformance, Warning, TEXT("GodfreyPerformerBridge setup: TargetSkeletalMesh is NOT set."));
	}

	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("GodfreyPerformerBridge setup: bEnableBodyMontages=%d (face/ACE independent)."),
		bEnableBodyMontages ? 1 : 0);

	LogSlot(TEXT("SpeakingStartMontage"), SpeakingStartMontage);
	LogSlot(TEXT("SpeakingIdleMontage"), SpeakingIdleMontage);
	LogSlot(TEXT("ReturnToIdleMontage"), ReturnToIdleMontage);
	LogSlot(TEXT("ThinkingMontage"), ThinkingMontage);
	LogSlot(TEXT("ListeningEnterMontage"), ListeningEnterMontage);
	LogSlot(TEXT("EmphasisMontage"), EmphasisMontage);
	LogSlot(TEXT("AmusedMontage"), AmusedMontage);
	LogSlot(TEXT("SeriousMontage"), SeriousMontage);
	LogSlot(TEXT("IdleBreathingMontage"), IdleBreathingMontage);
}

#if WITH_EDITOR
void UGodfreyPerformerAnimationBridgeComponent::EnsureEditorShirtDiagnosticTicker()
{
	if (!bLogMetaHumanShirtDiagnostics)
	{
		return;
	}

	if (EditorShirtDiagnosticTickerHandle.IsValid())
	{
		return;
	}

	EditorShirtDiagnosticTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UGodfreyPerformerAnimationBridgeComponent::EditorShirtDiagnosticTickerPoll),
		0.05f);
}

void UGodfreyPerformerAnimationBridgeComponent::RemoveEditorShirtDiagnosticTicker()
{
	if (EditorShirtDiagnosticTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(EditorShirtDiagnosticTickerHandle);
		EditorShirtDiagnosticTickerHandle.Reset();
	}
}

bool UGodfreyPerformerAnimationBridgeComponent::EditorShirtDiagnosticTickerPoll(float /*DeltaTime*/)
{
	if (!IsValid(this) || !bLogMetaHumanShirtDiagnostics)
	{
		RemoveEditorShirtDiagnosticTicker();
		return false;
	}

	const UWorld* const World = GetWorld();
	if (!World || World->IsGameWorld())
	{
		return true;
	}

	if (HasMetaHumanGarmentPostProcessMesh())
	{
		if (ShouldManageMetaHumanGarmentsAtRuntime())
		{
			MaintainMetaHumanCopyPoseBodySource();
		}
		MaybeLogMetaHumanShirtDiagnostics(TEXT("EditorTicker"));
	}
	else
	{
		MaybeLogMetaHumanShirtDiagnostics(TEXT("EditorTicker"));
	}

	return true;
}
#endif

void UGodfreyPerformerAnimationBridgeComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
#if WITH_EDITOR
	RemoveEditorShirtDiagnosticTicker();
#endif
	CancelOperatorCaptureTimers();
	if (bOperatorPerformanceHold)
	{
		StopFaceCurveOverlay();
		EndTakeRecorderIfPossible();
		ResumeInteractiveExhibit();
		bOperatorPerformanceHold = false;
	}
	UnregisterGodfreyOperatorCaptureSlateInput();
	UnbindPerformerState();
	StopIdleBreathingMontageIfActive();
	if (UWorld* const World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ClothingSkinnedPosePinTimerHandle);
	}
	bMetaHumanClothingRefreshPassesScheduled = false;
	bEditorCopyPoseStabilizeScheduled = false;
	MetaHumanGarmentRegisterWaitFrames = 0;
	Super::EndPlay(EndPlayReason);
}

void UGodfreyPerformerAnimationBridgeComponent::RefreshBehaviourTick()
{
	UpdatePerformerTickEnabled();
}

void UGodfreyPerformerAnimationBridgeComponent::SetCurrentAttentionTarget(AActor* NewTarget)
{
	AActor* const Old = CurrentAttentionTarget.Get();
	if (Old != NewTarget)
	{
		UE_LOG(LogGodfreyPerformance, Log, TEXT("GodfreyPerformerBridge: attention target %s -> %s"),
			Old ? *Old->GetName() : TEXT("(none)"), NewTarget ? *NewTarget->GetName() : TEXT("(none)"));
	}
	CurrentAttentionTarget = NewTarget;
}

void UGodfreyPerformerAnimationBridgeComponent::UpdatePerformerTickEnabled()
{
	const bool bEditorCopyPosePreview =
		ShouldManageMetaHumanGarmentsAtRuntime() && IsEditorViewportWorld() && HasMetaHumanGarmentPostProcessMesh();
	const bool bWantVisitorLookAt = bEnableVisitorEyeLookAt && ShouldForceConversationCameraFacing();
	const bool bWantConversationFacing = bStrictConversationCameraFacing && ShouldForceConversationCameraFacing();
	const bool bWantConversationHeadAim =
		GetDefault<UUnrealPerformerGodfreySettings>()->bGodfreyConversationHeadAim
		&& ShouldForceConversationCameraFacing();
	const bool bWantTick =
		bEnableIdleMicroMotion || bEnableAttentionTargetFollow || bIsSpeaking
		|| (bHoldingCameraFocusWhileAwaitingReply && bIsListening)
		|| bWantVisitorLookAt
		|| bWantConversationFacing
		|| bWantConversationHeadAim
		|| ShouldManageMetaHumanGarmentsAtRuntime() || bEditorCopyPosePreview || bKeepBodyMeshVisible
		|| bEnableDebugPerformancePlayKey || bOperatorPerformanceHold;
	SetComponentTickEnabled(bWantTick);
	if (bWantTick)
	{
		UE_LOG(LogGodfreyPerformance, Log,
			TEXT("GodfreyPerformerBridge: behaviour tick enabled (IdleMicro=%d Attention=%d editorCopyPose=%d)."),
			bEnableIdleMicroMotion, bEnableAttentionTargetFollow, bEditorCopyPosePreview ? 1 : 0);
	}
}

void UGodfreyPerformerAnimationBridgeComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	PollOperatorCaptureKey();
	EnsureBodyAnimBlueprintMode(TEXT("Tick"));
	if (bOperatorPerformanceHold)
	{
		TickFaceCurveOverlay();
	}
	else
	{
		TryRecoverStuckEngageChain(DeltaTime);
	}

	if (bEnableIdleMicroMotion)
	{
		IdleMicroTimeSeconds += DeltaTime;
		const float BreathSpeed = 1.15f * FMath::Max(0.15f, IdleBreathingIntensity);
		const float SwaySpeed = 0.52f * FMath::Max(0.15f, IdleBreathingIntensity);
		IdleBreathingWave = FMath::Sin(IdleMicroTimeSeconds * BreathSpeed);
		IdlePostureSwayWave = FMath::Sin(IdleMicroTimeSeconds * SwaySpeed + 1.1f) * 0.65f;
	}

	if (bEnableAttentionTargetFollow)
	{
		UpdateAttentionRotation(DeltaTime);
	}

	UpdateVisitorHeadLookAt();

	if (bTravelRootMotionActive && IsValid(TargetSkeletalMesh))
	{
		const FRootMotionMovementParams RootMotion = TargetSkeletalMesh->ConsumeRootMotion();
		if (RootMotion.bHasRootMotion)
		{
			if (AActor* const Owner = GetOwner())
			{
				const FTransform Delta = RootMotion.GetRootMotionTransform();
				Owner->AddActorWorldOffset(Delta.GetTranslation(), false, nullptr, ETeleportType::None);
				const float YawDelta = Delta.Rotator().Yaw;
				if (!FMath::IsNearlyZero(YawDelta))
				{
					Owner->AddActorWorldRotation(FRotator(0.f, YawDelta, 0.f), false, nullptr, ETeleportType::None);
				}
			}
		}
		if (UAnimInstance* const AnimInst = TargetSkeletalMesh->GetAnimInstance())
		{
			if (ActiveTravelMontage && !AnimInst->Montage_IsActive(ActiveTravelMontage))
			{
				StopTravelRootMotionIfActive(TEXT("travel montage ended"));
			}
		}
	}

	if (bIsSpeaking)
	{
		MaintainSpeakingIdleMontage();
	}
	else if (bHoldingCameraFocusWhileAwaitingReply && bIsListening && !bIsSpeaking)
	{
		MaintainAwaitingConversationHoldMontage();
	}

	if (bKeepBodyMeshVisible && IsValid(TargetSkeletalMesh))
	{
		TargetSkeletalMesh->SetHiddenInGame(false, true);
		TargetSkeletalMesh->SetVisibility(true, true);
	}

	if (ShouldManageMetaHumanGarmentsAtRuntime() && IsEditorViewportWorld() && HasMetaHumanGarmentPostProcessMesh())
	{
		MaintainMetaHumanCopyPoseBodySource();
		return;
	}

	if (ShouldManageClothingLeaderPose())
	{
		MaintainClothingLeaderPose();
	}
	else if (ShouldManageMetaHumanGarmentsAtRuntime())
	{
		MaintainMetaHumanBodyTickForClothing();
	}
}

bool UGodfreyPerformerAnimationBridgeComponent::IsBodyMontagePlaying() const
{
	if (!IsValid(TargetSkeletalMesh))
	{
		return false;
	}

	if (UAnimInstance* AnimInst = TargetSkeletalMesh->GetAnimInstance())
	{
		return AnimInst->IsAnyMontagePlaying();
	}

	return false;
}

void UGodfreyPerformerAnimationBridgeComponent::MaintainClothingLeaderPose()
{
	if (UsesMetaHumanNativeClothingPipeline() || !HasClothingFollowerMeshesOnBody() || !IsValid(TargetSkeletalMesh))
	{
		return;
	}

	USkeletalMeshComponent* Body = TargetSkeletalMesh;
	Body->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	Body->bEnableUpdateRateOptimizations = false;
	Body->SetHiddenInGame(bKeepBodyMeshVisible ? false : true, true);
	Body->SetVisibility(true, true);

#if WITH_EDITOR
	Body->SetUpdateAnimationInEditor(true);
#endif

	if (!Body->GetAnimInstance() && !IsBodyMontagePlaying())
	{
		const bool bWasForceRefPose = Body->bForceRefpose;
		Body->bForceRefpose = true;
		Body->RefreshBoneTransforms();
		Body->bForceRefpose = bWasForceRefPose;
	}
	else
	{
		Body->RefreshBoneTransforms();
	}

	UClass* const ClothingPostProcessClass = LoadClothingPostProcessAnimClass();
	for (const FName FollowerName : ClothingFollowerMeshNames)
	{
		USkeletalMeshComponent* Follower = FindFollowerMeshByComponentName(FollowerName);
		if (!IsValid(Follower) || !HasRenderableSkeletalMeshAsset(Follower))
		{
			continue;
		}

		if (Follower->LeaderPoseComponent.Get() != Body)
		{
			Follower->SetLeaderPoseComponent(Body, true, true);
		}

		if (Follower->VisibilityBasedAnimTickOption != EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones)
		{
			Follower->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		}

		Follower->bEnableUpdateRateOptimizations = false;
		Follower->SetDisablePostProcessBlueprint(false);

#if WITH_EDITOR
		Follower->SetUpdateAnimationInEditor(true);
#endif

		if (ClothingPostProcessClass
			&& Follower->GetPostProcessInstance()
			&& Follower->GetPostProcessInstance()->GetClass() != ClothingPostProcessClass)
		{
			Follower->SetOverridePostProcessAnimBP(ClothingPostProcessClass, true);
		}
		else if (ClothingPostProcessClass && !Follower->GetPostProcessInstance())
		{
			Follower->SetOverridePostProcessAnimBP(ClothingPostProcessClass, true);
		}

		if (Follower->GetPostProcessInstance())
		{
			Follower->TickAnimation(0.f, false);
			Follower->RefreshBoneTransforms();
		}
		else
		{
			Follower->UpdateFollowerComponent();
		}
	}

	Body->RefreshFollowerComponents();
}

void UGodfreyPerformerAnimationBridgeComponent::EnsureFaceEyeLookAtPostProcess()
{
	// Never install Face PP override on MetaHuman — it severs head/body attach and kills ACE lipsync.
	RestoreMetaHumanFacePostProcessIfNeeded();
	if (bEnableVisitorEyeLookAt)
	{
		UE_LOG(LogGodfreyPerformance, Warning,
			TEXT("GodfreyPerformerBridge: bEnableVisitorEyeLookAt ignored — Face post-process gaze is disabled (breaks MetaHuman face + ACE)."));
	}
}

bool UGodfreyPerformerAnimationBridgeComponent::ShouldForceConversationCameraFacing() const
{
	if (bIsSpeaking || bIsListening || bIsThinking || bHoldingCameraFocusWhileAwaitingReply)
	{
		return true;
	}
	if (PerformerState)
	{
		const EGodfreyExhibitionPresence Presence = PerformerState->GetExhibitionPresence();
		return Presence == EGodfreyExhibitionPresence::Conversing || Presence == EGodfreyExhibitionPresence::Engaging;
	}
	return false;
}

void UGodfreyPerformerAnimationBridgeComponent::UpdateVisitorHeadLookAt()
{
	// Face post-process gaze stays off (breaks MetaHuman attach + ACE). Body neck LookAt is safe.
	RestoreMetaHumanFacePostProcessIfNeeded();

	UGodfreyBodyAnimInstance* const BodyAnim = IsValid(TargetSkeletalMesh)
		? Cast<UGodfreyBodyAnimInstance>(TargetSkeletalMesh->GetAnimInstance())
		: nullptr;
	if (!BodyAnim)
	{
		return;
	}

	const UUnrealPerformerGodfreySettings* const Settings = GetDefault<UUnrealPerformerGodfreySettings>();
	FVector CameraLocation = FVector::ZeroVector;
	const bool bAim =
		Settings
		&& Settings->bGodfreyConversationHeadAim
		&& ShouldForceConversationCameraFacing()
		&& !bOperatorPerformanceHold
		&& TryGetPrimaryCameraLocation(this, CameraLocation);

	BodyAnim->SetConversationHeadAim(
		bAim,
		CameraLocation,
		Settings ? Settings->GodfreyConversationHeadAimClampDegrees : 16.f,
		Settings ? Settings->GodfreyConversationHeadAimAlpha : 0.f);
}

void UGodfreyPerformerAnimationBridgeComponent::RestoreMetaHumanFacePostProcessIfNeeded()
{
	USkeletalMeshComponent* Face = FindFollowerMeshByComponentName(FName(TEXT("Face")));
	if (!IsValid(Face))
	{
		return;
	}

	UAnimInstance* const ExistingPP = Face->GetPostProcessInstance();
	const TSubclassOf<UAnimInstance> PostProcessClass = Face->GetPostProcessAnimBPClassToBeUsed();
	const bool bHasOurOverride =
		(ExistingPP && ExistingPP->IsA<UGodfreyFaceEyeLookAtAnimInstance>())
		|| (PostProcessClass && PostProcessClass->IsChildOf(UGodfreyFaceEyeLookAtAnimInstance::StaticClass()));

	if (!bHasOurOverride)
	{
		return;
	}

	Face->SetOverridePostProcessAnimBP(nullptr, true);
	Face->SetDisablePostProcessBlueprint(false);
	Face->TickAnimation(0.f, false);
	Face->RefreshBoneTransforms();
	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("GodfreyPerformerBridge: cleared GodfreyFaceEyeLookAt post-process on '%s' (MetaHuman Face + ACE restored)."),
		*Face->GetName());
}

void UGodfreyPerformerAnimationBridgeComponent::UpdateAttentionRotation(const float DeltaTime)
{
	AActor* const Owner = GetOwner();
	if (!Owner || !bHasCachedExhibitYaw)
	{
		return;
	}
	if (!IsValid(CurrentAttentionTarget.Get()))
	{
		UE_LOG(LogGodfreyPerformance, Verbose,
			TEXT("GodfreyPerformerBridge: attention follow active but no valid target; holding exhibit yaw %.2f."),
			CachedExhibitYawDegrees);
	}

	const float Speed = FMath::Max(0.1f, AttentionInterpSpeed);
	FRotator Current = Owner->GetActorRotation();

	float GoalYawDegrees = CachedExhibitYawDegrees;
	if (bIsSpeaking || bHoldingCameraFocusWhileAwaitingReply
		|| (bStrictConversationCameraFacing && ShouldForceConversationCameraFacing()))
	{
		float FacingYaw = 0.f;
		if (TryGetExhibitionFacingYaw(FacingYaw))
		{
			GoalYawDegrees = FacingYaw;
		}
		else if (bIsSpeaking || bHoldingCameraFocusWhileAwaitingReply)
		{
			UE_LOG(LogGodfreyPerformance, Warning,
				TEXT("GodfreyPerformerBridge: no primary camera/view rotation while speaking/awaiting reply."));
		}
	}
	else if (IsValid(CurrentAttentionTarget))
	{
		FVector Delta = CurrentAttentionTarget->GetActorLocation() - Owner->GetActorLocation();
		Delta.Z = 0.f;
		if (!Delta.IsNearlyZero(1.f))
		{
			const float ToYaw = Delta.Rotation().Yaw;
			float Offset = FRotator::NormalizeAxis(ToYaw - CachedExhibitYawDegrees);
			Offset = FMath::Clamp(Offset, -AttentionOffsetStrength, AttentionOffsetStrength);
			GoalYawDegrees = CachedExhibitYawDegrees + Offset;
		}
	}

	const FRotator TargetRot(Current.Pitch, GoalYawDegrees, Current.Roll);
	const bool bHardCameraLock =
		bIsSpeaking || bHoldingCameraFocusWhileAwaitingReply
		|| (bStrictConversationCameraFacing && ShouldForceConversationCameraFacing());
	const FRotator NewRot = bHardCameraLock ? TargetRot : FMath::RInterpTo(Current, TargetRot, DeltaTime, Speed);
	if (!NewRot.Equals(Current, 0.05f))
	{
		Owner->SetActorRotation(NewRot);
	}
}

bool UGodfreyPerformerAnimationBridgeComponent::TryGetExhibitionFacingYaw(float& OutYawDegrees)
{
	OutYawDegrees = 0.f;
	EnsureFacingOffsetCalibrated();

	if (bLockBodyYawToExhibitFacingDuringConversation && bHasCachedExhibitYaw)
	{
		OutYawDegrees = FRotator::NormalizeAxis(CachedExhibitYawDegrees + ExhibitionBodyYawOffsetDegrees);
		return true;
	}

	float RawFacingYaw = 0.f;
	if (!TryComputeRawExhibitionFacingYaw(this, RawFacingYaw))
	{
		return false;
	}

	// Raw look-at is actor-+X math. ExhibitionBodyYawOffsetDegrees maps that to visual face
	// (calibrated so sea-idle placement already aims at Exhibit_CineCamera).
	float ActorYaw = FRotator::NormalizeAxis(RawFacingYaw + ExhibitionBodyYawOffsetDegrees);

	if (bAutoApplyMeshRelativeYawOffset && bHasCachedMeshVisualYawOffset)
	{
		ActorYaw = FRotator::NormalizeAxis(ActorYaw - CachedMeshVisualYawOffsetDegrees);
	}

	if (bHasCachedExhibitYaw)
	{
		float Delta = FRotator::NormalizeAxis(ActorYaw - CachedExhibitYawDegrees);
		Delta = FMath::Clamp(Delta, -MaxExhibitionFacingDeltaFromSeaIdle, MaxExhibitionFacingDeltaFromSeaIdle);
		ActorYaw = FRotator::NormalizeAxis(CachedExhibitYawDegrees + Delta);
	}

	OutYawDegrees = ActorYaw;
	return true;
}

bool UGodfreyPerformerAnimationBridgeComponent::EnsureFacingOffsetCalibrated()
{
	if (bFacingOffsetCalibrated || !bCalibrateFacingOffsetFromSeaIdlePlacement)
	{
		return bFacingOffsetCalibrated;
	}
	if (!bHasCachedExhibitYaw)
	{
		return false;
	}

	float RawLookAtYaw = 0.f;
	if (!TryComputeRawExhibitionFacingYaw(this, RawLookAtYaw))
	{
		return false;
	}

	// Sea-idle actor yaw is the placed "looking at visitor/camera" pose. Look-at yaw is actor-+X math.
	// Offset makes: LookAtYaw + Offset == CachedExhibitYaw.
	ExhibitionBodyYawOffsetDegrees = FRotator::NormalizeAxis(CachedExhibitYawDegrees - RawLookAtYaw);
	bFacingOffsetCalibrated = true;

	FVector CamLoc = FVector::ZeroVector;
	FRotator CamRot = FRotator::ZeroRotator;
	TryGetExhibitionCameraTransform(this, CamLoc, CamRot);
	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("GodfreyPerformerBridge: calibrated facing offset=%.2f (exhibitYaw=%.2f rawLookAt=%.2f camLoc=(%.1f,%.1f,%.1f))."),
		ExhibitionBodyYawOffsetDegrees,
		CachedExhibitYawDegrees,
		RawLookAtYaw,
		CamLoc.X, CamLoc.Y, CamLoc.Z);
	return true;
}

void UGodfreyPerformerAnimationBridgeComponent::CacheMeshVisualYawOffset()
{
	bHasCachedMeshVisualYawOffset = false;
	CachedMeshVisualYawOffsetDegrees = 0.f;
	if (!bAutoApplyMeshRelativeYawOffset)
	{
		return;
	}
	if (!IsValid(TargetSkeletalMesh) || !GetOwner())
	{
		return;
	}
	FVector Forward = TargetSkeletalMesh->GetForwardVector();
	Forward.Z = 0.f;
	if (Forward.IsNearlyZero(KINDA_SMALL_NUMBER))
	{
		return;
	}
	Forward.Normalize();
	const float MeshWorldYaw = Forward.Rotation().Yaw;
	const float ActorYaw = GetOwner()->GetActorRotation().Yaw;
	CachedMeshVisualYawOffsetDegrees = FRotator::NormalizeAxis(MeshWorldYaw - ActorYaw);
	bHasCachedMeshVisualYawOffset = true;
	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("GodfreyPerformerBridge: cached mesh visual yaw offset %.2f (actorYaw=%.2f meshForwardYaw=%.2f)."),
		CachedMeshVisualYawOffsetDegrees,
		ActorYaw,
		MeshWorldYaw);
}

void UGodfreyPerformerAnimationBridgeComponent::StopAllBodyMontages(const TCHAR* Reason)
{
	if (!bEnforceSingleActiveBodyMontage)
	{
		return;
	}
	UAnimInstance* const AnimInst = ResolveAnimInstance(TEXT("StopAllBodyMontages"));
	if (!AnimInst || !AnimInst->IsAnyMontagePlaying())
	{
		return;
	}
	AnimInst->Montage_Stop(GetBodyMontageBlendOut());
	ActiveSpeakingIdlePlayMontage = nullptr;
	ActivePlantedStanceMontage = nullptr;
	ActiveTravelMontage = nullptr;
	bTravelRootMotionActive = false;
	SetAnimInstanceIgnoreRootMotion(true);
	EnsurePlantedStancePlaying();
	SpeakingIdleMontageCycleSeconds = 0.f;
	SpeakingIdleMontageWallCycleSeconds = 0.f;
	SpeakingIdleCycleStartWorldTime = -1.0;
	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("GodfreyPerformerBridge [SeqChain]: stopped all body montages before '%s'."),
		Reason ? Reason : TEXT("(none)"));
}

void UGodfreyPerformerAnimationBridgeComponent::TryBindPerformerState()
{
	AActor* const Owner = GetOwner();
	if (!Owner)
	{
		UE_LOG(LogGodfreyPerformance, Warning,
			TEXT("GodfreyPerformerBridge: cannot bind — no owner. Add this component to BP_Gavin / character actor."));
		return;
	}

	PerformerState = Owner->FindComponentByClass<UGodfreyPerformanceStateComponent>();
	if (!PerformerState)
	{
		UE_LOG(LogGodfreyPerformance, Warning,
			TEXT("GodfreyPerformerBridge: UGodfreyPerformanceStateComponent not found on actor '%s'. Bridge will not receive events."),
			*Owner->GetName());
		return;
	}

	PerformerState->OnListeningStarted.AddDynamic(this, &UGodfreyPerformerAnimationBridgeComponent::HandleListeningStarted);
	PerformerState->OnThinkingStarted.AddDynamic(this, &UGodfreyPerformerAnimationBridgeComponent::HandleThinkingStarted);
	PerformerState->OnSpeakingStarted.AddDynamic(this, &UGodfreyPerformerAnimationBridgeComponent::HandleSpeakingStarted);
	PerformerState->OnSpeakingEnded.AddDynamic(this, &UGodfreyPerformerAnimationBridgeComponent::HandleSpeakingEnded);
	PerformerState->OnReturnedToIdle.AddDynamic(this, &UGodfreyPerformerAnimationBridgeComponent::HandleReturnedToIdle);
	PerformerState->OnEmphasisTriggered.AddDynamic(this, &UGodfreyPerformerAnimationBridgeComponent::HandleEmphasisTriggered);
	PerformerState->OnAmusedTriggered.AddDynamic(this, &UGodfreyPerformerAnimationBridgeComponent::HandleAmusedTriggered);
	PerformerState->OnSeriousTriggered.AddDynamic(this, &UGodfreyPerformerAnimationBridgeComponent::HandleSeriousTriggered);
	PerformerState->OnPerformanceCueReceived.AddDynamic(this,
		&UGodfreyPerformerAnimationBridgeComponent::HandlePerformanceCueReceived);
	PerformerState->OnSeaIdleStarted.AddDynamic(this, &UGodfreyPerformerAnimationBridgeComponent::HandleSeaIdleStarted);
	PerformerState->OnEngageSequenceStarted.AddDynamic(this,
		&UGodfreyPerformerAnimationBridgeComponent::HandleEngageSequenceStarted);
	PerformerState->OnFarewellSequenceStarted.AddDynamic(this,
		&UGodfreyPerformerAnimationBridgeComponent::HandleFarewellSequenceStarted);

	RefreshMirroredPerformanceState();
	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("GodfreyPerformerBridge: bound to UGodfreyPerformanceStateComponent on '%s' (CurrentPerformanceState=%d)."),
		*Owner->GetName(), static_cast<int32>(CurrentPerformanceState));

	// PerformanceState may have entered SeaIdle before this component bound.
	if (bDriveExhibitionPresenceMontages
		&& PerformerState->GetExhibitionPresence() == EGodfreyExhibitionPresence::SeaIdle)
	{
		PlaySeaIdleLoop();
	}
}

void UGodfreyPerformerAnimationBridgeComponent::UnbindPerformerState()
{
	if (!PerformerState)
	{
		return;
	}

	PerformerState->OnListeningStarted.RemoveDynamic(this, &UGodfreyPerformerAnimationBridgeComponent::HandleListeningStarted);
	PerformerState->OnThinkingStarted.RemoveDynamic(this, &UGodfreyPerformerAnimationBridgeComponent::HandleThinkingStarted);
	PerformerState->OnSpeakingStarted.RemoveDynamic(this, &UGodfreyPerformerAnimationBridgeComponent::HandleSpeakingStarted);
	PerformerState->OnSpeakingEnded.RemoveDynamic(this, &UGodfreyPerformerAnimationBridgeComponent::HandleSpeakingEnded);
	PerformerState->OnReturnedToIdle.RemoveDynamic(this, &UGodfreyPerformerAnimationBridgeComponent::HandleReturnedToIdle);
	PerformerState->OnEmphasisTriggered.RemoveDynamic(this, &UGodfreyPerformerAnimationBridgeComponent::HandleEmphasisTriggered);
	PerformerState->OnAmusedTriggered.RemoveDynamic(this, &UGodfreyPerformerAnimationBridgeComponent::HandleAmusedTriggered);
	PerformerState->OnSeriousTriggered.RemoveDynamic(this, &UGodfreyPerformerAnimationBridgeComponent::HandleSeriousTriggered);
	PerformerState->OnPerformanceCueReceived.RemoveDynamic(this,
		&UGodfreyPerformerAnimationBridgeComponent::HandlePerformanceCueReceived);
	PerformerState->OnSeaIdleStarted.RemoveDynamic(this, &UGodfreyPerformerAnimationBridgeComponent::HandleSeaIdleStarted);
	PerformerState->OnEngageSequenceStarted.RemoveDynamic(this,
		&UGodfreyPerformerAnimationBridgeComponent::HandleEngageSequenceStarted);
	PerformerState->OnFarewellSequenceStarted.RemoveDynamic(this,
		&UGodfreyPerformerAnimationBridgeComponent::HandleFarewellSequenceStarted);

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(EngageChainTimerHandle);
		World->GetTimerManager().ClearTimer(FarewellChainTimerHandle);
		World->GetTimerManager().ClearTimer(PresenceIdleRetryTimerHandle);
		World->GetTimerManager().ClearTimer(SeaIdleChainTimerHandle);
	}

	UE_LOG(LogGodfreyPerformance, Log, TEXT("GodfreyPerformerBridge: unbound from performer state."));
	PerformerState = nullptr;
}

void UGodfreyPerformerAnimationBridgeComponent::RefreshMirroredPerformanceState()
{
	if (PerformerState)
	{
		CurrentPerformanceState = PerformerState->GetPerformanceState();
	}
}

UAnimInstance* UGodfreyPerformerAnimationBridgeComponent::ResolveAnimInstance(const TCHAR* ContextLabel) const
{
	if (!IsValid(TargetSkeletalMesh))
	{
		UE_LOG(LogGodfreyPerformance, Warning, TEXT("GodfreyPerformerBridge [%s]: TargetSkeletalMesh is not set."), ContextLabel);
		return nullptr;
	}
	UAnimInstance* const AnimInst = TargetSkeletalMesh->GetAnimInstance();
	if (!AnimInst)
	{
		UE_LOG(LogGodfreyPerformance, Warning, TEXT("GodfreyPerformerBridge [%s]: no AnimInstance on mesh '%s' (is the mesh visible / begun play?)."),
			ContextLabel, *TargetSkeletalMesh->GetName());
		return nullptr;
	}
	return AnimInst;
}

void UGodfreyPerformerAnimationBridgeComponent::LogActingCue(const FString& CueType, const FString& CueValue,
	const FString& RawCue) const
{
	FString SpeechId = TEXT("(none)");
	if (const UGodfreyDiagnosticsSubsystem* Diag = UGodfreyDiagnosticsSubsystem::Get(this))
	{
		SpeechId = Diag->GetCurrentSpeechId();
		if (SpeechId.IsEmpty())
		{
			SpeechId = TEXT("(none)");
		}
	}

	const double WorldTimeSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0;
	UE_LOG(LogGodfreyAnimation, Log,
		TEXT("[Acting] cue | t=%.3f | SpeechId=%s | type=%s | value=%s | raw=%s"),
		WorldTimeSeconds,
		*SpeechId,
		CueType.IsEmpty() ? TEXT("(none)") : *CueType,
		CueValue.IsEmpty() ? TEXT("(none)") : *CueValue,
		RawCue.IsEmpty() ? TEXT("(none)") : *RawCue.Left(120));
}

void UGodfreyPerformerAnimationBridgeComponent::LogActingPlay(const TCHAR* ContextLabel, const UAnimMontage* Montage,
	const UAnimSequence* Sequence, const float PlayRate, const bool bLoop, const float WallLenSeconds,
	const bool bPlayed) const
{
	FString SpeechId = TEXT("(none)");
	if (const UGodfreyDiagnosticsSubsystem* Diag = UGodfreyDiagnosticsSubsystem::Get(this))
	{
		SpeechId = Diag->GetCurrentSpeechId();
		if (SpeechId.IsEmpty())
		{
			SpeechId = TEXT("(none)");
		}
	}

	const double WorldTimeSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0;
	UE_LOG(LogGodfreyAnimation, Log,
		TEXT("[Acting] play | t=%.3f | SpeechId=%s | context=%s | sequence=%s | montage=%s | cueType=%s | cueValue=%s | loop=%d | rate=%.2f | wallLen=%.2f | played=%d"),
		WorldTimeSeconds,
		*SpeechId,
		ContextLabel ? ContextLabel : TEXT("(none)"),
		Sequence ? *Sequence->GetName() : TEXT("(none)"),
		Montage ? *Montage->GetName() : TEXT("(none)"),
		LastActingCueType.IsEmpty() ? TEXT("(none)") : *LastActingCueType,
		LastActingCueValue.IsEmpty() ? TEXT("(none)") : *LastActingCueValue,
		bLoop ? 1 : 0,
		PlayRate,
		WallLenSeconds,
		bPlayed ? 1 : 0);
}

bool UGodfreyPerformerAnimationBridgeComponent::PlayMontageIfPossible(UAnimMontage* Montage, const TCHAR* ContextLabel,
	const float PlayRate, const bool bRestartIfAlreadyPlaying, const bool bLoopMontage, const bool bChainAsHold,
	const bool bSoftSlotReplace, const bool bApplyRootMotion)
{
	if (!bEnableBodyMontages)
	{
		UE_LOG(LogGodfreyPerformance, Verbose,
			TEXT("GodfreyPerformerBridge [%s]: body montages disabled (bEnableBodyMontages=0) — skipping."), ContextLabel);
		return false;
	}
	if (!Montage)
	{
		UE_LOG(LogGodfreyPerformance, Warning, TEXT("GodfreyPerformerBridge [%s]: no montage assigned; skipping play."), ContextLabel);
		return false;
	}

	Montage = RemapMontageToEyeFixedVariant(Montage);

	EnsureMontageAnimInstanceReady();
	EnsureBodyMontagePlaybackReady();
	UAnimInstance* const AnimInst = ResolveAnimInstance(ContextLabel);
	if (!AnimInst)
	{
		return false;
	}

	const bool bFullBodyOverride = IsFullBodyOverrideContext(ContextLabel);
	const bool bUseUpperBody = !bApplyRootMotion && !bFullBodyOverride;
	const FName TargetSlot = bUseUpperBody
		? GetInPlaceOverlaySlotName()
		: UGodfreyBodyAnimInstance::DefaultBodyMontageSlotName;

	UAnimMontage* const PlayMontage = bLoopMontage
		? ResolveLoopedSlotMontage(Montage, TargetSlot, ContextLabel)
		: ResolveMontageForSlot(Montage, TargetSlot, ContextLabel);
	if (!PlayMontage)
	{
		return false;
	}

	if (UAnimSequence* PlaySeqMutable = ExtractPrimarySequenceFromMontage(PlayMontage))
	{
		ConfigureSequenceRootHandling(PlaySeqMutable, bApplyRootMotion);
	}

	if (bUseUpperBody)
	{
		StopTravelRootMotionIfActive(ContextLabel);
		EnsurePlantedStancePlaying();
		if (UGodfreyBodyAnimInstance* const BodyAnim = Cast<UGodfreyBodyAnimInstance>(AnimInst))
		{
			BodyAnim->SetUpperBodyLayerWeight(
				GetDefault<UUnrealPerformerGodfreySettings>()->GodfreyUpperBodyMontageBlendWeight);
		}
	}
	else
	{
		if (UGodfreyBodyAnimInstance* const BodyAnim = Cast<UGodfreyBodyAnimInstance>(AnimInst))
		{
			BodyAnim->SetUpperBodyLayerWeight(0.f);
		}
		if (UAnimMontage* const PrevUpper = ActiveSpeakingIdlePlayMontage.Get())
		{
			if (AnimInst->Montage_IsActive(PrevUpper)
				&& PrevUpper->SlotAnimTracks.Num() > 0
				&& PrevUpper->SlotAnimTracks[0].SlotName == UGodfreyBodyAnimInstance::UpperBodyMontageSlotName)
			{
				AnimInst->Montage_Stop(GetBodyMontageBlendOut(), PrevUpper);
			}
			ActiveSpeakingIdlePlayMontage = nullptr;
		}
	}

	if (bApplyRootMotion)
	{
		bTravelRootMotionActive = true;
		SetAnimInstanceIgnoreRootMotion(false);
	}
	else
	{
		SetAnimInstanceIgnoreRootMotion(true);
	}

	if (!bRestartIfAlreadyPlaying && bDeduplicateActiveMontagePlays && AnimInst->Montage_IsActive(PlayMontage))
	{
		UE_LOG(LogGodfreyPerformance, Verbose, TEXT("GodfreyPerformerBridge [%s]: montage '%s' already active; skip restart."),
			ContextLabel, *PlayMontage->GetName());
		return false;
	}

	const bool bSeaIdleHold = IsSeaIdleHoldContext(ContextLabel);
	const bool bSpeakingHold = ContextLabel && FCString::Stricmp(ContextLabel, TEXT("SpeakingIdle")) == 0;
	const bool bDialogIdle = !bSpeakingHold
		&& (IsDialogIdleHoldContext(ContextLabel) || (bSoftSlotReplace && !bSeaIdleHold));
	const EGodfreyIdleBlendProfile BlendProfile = bSeaIdleHold
		? EGodfreyIdleBlendProfile::SeaIdle
		: (bDialogIdle ? EGodfreyIdleBlendProfile::DialogIdle : EGodfreyIdleBlendProfile::Body);
	ApplyBodyMontageBlendTimes(PlayMontage, BlendProfile);
	// Overlay plays must not stop DefaultSlot planted stance (default bStopAllMontages=true
	// was blending the whole figure toward RefPose / A-pose between clips).
	const float PlayLength = AnimInst->Montage_Play(
		PlayMontage,
		FMath::Max(0.05f, PlayRate),
		EMontagePlayReturnType::MontageLength,
		0.f,
		/*bStopAllMontages=*/!bUseUpperBody);
	const double WorldTimeSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0;
	const UAnimSequence* const PlaySeq = ExtractPrimarySequenceFromMontage(PlayMontage);
	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("GodfreyPerformerBridge [SeqStart]: t=%.3f context=%s montage='%s' source='%s' sequence='%s' rate=%.2f loop=%d chainHold=%d soft=%d wallLen=%.2f slot=%s travel=%d"),
		WorldTimeSeconds,
		ContextLabel ? ContextLabel : TEXT("(none)"),
		*PlayMontage->GetName(),
		*Montage->GetName(),
		PlaySeq ? *PlaySeq->GetName() : TEXT("(none)"),
		PlayRate,
		bLoopMontage ? 1 : 0,
		bChainAsHold ? 1 : 0,
		bSoftSlotReplace ? 1 : 0,
		PlayLength,
		*TargetSlot.ToString(),
		bApplyRootMotion ? 1 : 0);
	LogActingPlay(ContextLabel, PlayMontage, PlaySeq, PlayRate, bLoopMontage, PlayLength, PlayLength > KINDA_SMALL_NUMBER);

	if (bUseUpperBody)
	{
		if (UAnimMontage* const Prev = ActiveSpeakingIdlePlayMontage.Get())
		{
			if (Prev != PlayMontage && AnimInst->Montage_IsActive(Prev))
			{
				const float SoftOut = bSeaIdleHold
					? GetSeaIdleMontageBlendOut()
					: ((bDialogIdle || bSpeakingHold) ? GetDialogIdleMontageBlendOut() : GetBodyMontageBlendOut());
				AnimInst->Montage_Stop(SoftOut, Prev);
			}
		}
	}
	else if (ActivePlantedStanceMontage && ActivePlantedStanceMontage != PlayMontage)
	{
		ActivePlantedStanceMontage = nullptr;
	}

	if (PlayLength > KINDA_SMALL_NUMBER)
	{
		if (UGodfreyDiagnosticsSubsystem* Diag = UGodfreyDiagnosticsSubsystem::Get(this))
		{
			Diag->SetCurrentAnimationName(PlayMontage->GetName());
			Diag->MarkStageForCurrent(EGodfreyUtteranceStage::BodyAnimStarted);
		}
	}

	if (bApplyRootMotion)
	{
		ActiveTravelMontage = PlayMontage;
		BindSpeakingIdleMontageEndDelegate(AnimInst, PlayMontage);
	}

	if (PlayLength > KINDA_SMALL_NUMBER && (bLoopMontage || bChainAsHold))
	{
		if (bLoopMontage)
		{
			ApplySpeakingMontageSectionLoop(AnimInst, PlayMontage);
		}
		// Montage_Play returns asset timeline length in this engine; wall clock is timeline / playRate.
		SpeakingIdleMontageCycleSeconds = PlayMontage->GetPlayLength();
		const float SafeRate = FMath::Max(0.05f, PlayRate);
		SpeakingIdleMontageWallCycleSeconds = SpeakingIdleMontageCycleSeconds / SafeRate;
		if (UWorld* World = GetWorld())
		{
			SpeakingIdleCycleStartWorldTime = World->GetTimeSeconds();
		}
		ActiveSpeakingIdlePlayMontage = PlayMontage;
		BindSpeakingIdleMontageEndDelegate(AnimInst, PlayMontage);
		if (bLoopMontage && PlayMontage->CompositeSections.Num() > 0)
		{
			UE_LOG(LogGodfreyPerformance, Log,
				TEXT("GodfreyPerformerBridge [%s]: montage '%s' section '%s' set to loop (timeline=%.2fs wall=%.2fs)."),
				ContextLabel,
				*PlayMontage->GetName(),
				*PlayMontage->CompositeSections[0].SectionName.ToString(),
				SpeakingIdleMontageCycleSeconds,
				SpeakingIdleMontageWallCycleSeconds);
		}
		else if (bChainAsHold)
		{
			UE_LOG(LogGodfreyPerformance, Log,
				TEXT("GodfreyPerformerBridge [%s]: one-shot hold '%s' (timeline=%.2fs wall=%.2fs rate=%.2f) — will chain to next pool AS."),
				ContextLabel,
				*PlayMontage->GetName(),
				SpeakingIdleMontageCycleSeconds,
				SpeakingIdleMontageWallCycleSeconds,
				SafeRate);
		}
	}

	FName MontageSlotName = NAME_None;
	if (PlayMontage->SlotAnimTracks.Num() > 0)
	{
		MontageSlotName = PlayMontage->SlotAnimTracks[0].SlotName;
	}

	const float SlotWeight = AnimInst->GetSlotMontageGlobalWeight(TargetSlot);
	const bool bMontagePlaying = AnimInst->IsAnyMontagePlaying();
	const float ReportedSlotWeight =
		SlotWeight > KINDA_SMALL_NUMBER ? SlotWeight : (bMontagePlaying ? 1.f : 0.f);
	const bool bSlotMatches = MontageSlotName.IsNone() || MontageSlotName == TargetSlot;

	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("GodfreyPerformerBridge [%s]: playing montage '%s' (source='%s' rate=%.2f len=%.2fs) anim='%s' montageSlot='%s' expectedSlot='%s' slotWeight=%.2f match=%d."),
		ContextLabel,
		*PlayMontage->GetName(),
		*Montage->GetName(),
		PlayRate,
		PlayLength,
		*AnimInst->GetClass()->GetName(),
		MontageSlotName.IsNone() ? TEXT("(none)") : *MontageSlotName.ToString(),
		*TargetSlot.ToString(),
		ReportedSlotWeight,
		bSlotMatches ? 1 : 0);

	if (PlayLength <= KINDA_SMALL_NUMBER)
	{
		UE_LOG(LogGodfreyPerformance, Warning,
			TEXT("GodfreyPerformerBridge [%s]: Montage_Play returned 0 — skeleton/slot mismatch or empty montage (slots: [%s])."),
			ContextLabel, *DescribeMontageSlotTracks(PlayMontage));
	}
	else if (!bSlotMatches)
	{
		UE_LOG(LogGodfreyPerformance, Warning,
			TEXT("GodfreyPerformerBridge [%s]: montage slot '%s' != expected slot '%s'."),
			ContextLabel,
			MontageSlotName.IsNone() ? TEXT("(none)") : *MontageSlotName.ToString(),
			*TargetSlot.ToString());
	}
	else if (ReportedSlotWeight <= KINDA_SMALL_NUMBER)
	{
		UE_LOG(LogGodfreyPerformance, Warning,
			TEXT("GodfreyPerformerBridge [%s]: montage playing but slot '%s' weight is ~0 — slot node may not be receiving this montage."),
			ContextLabel, *TargetSlot.ToString());
	}

	return PlayLength > KINDA_SMALL_NUMBER;
}

void UGodfreyPerformerAnimationBridgeComponent::StopIdleBreathingMontageIfActive()
{
	if (!IsValid(TargetSkeletalMesh) || !IdleBreathingMontage)
	{
		return;
	}
	UAnimInstance* const AnimInst = TargetSkeletalMesh->GetAnimInstance();
	if (!AnimInst)
	{
		return;
	}
	if (AnimInst->Montage_IsActive(IdleBreathingMontage))
	{
		AnimInst->Montage_Stop(GetSpeakingIdleMontageBlendOut(), IdleBreathingMontage);
		UE_LOG(LogGodfreyPerformance, Log, TEXT("GodfreyPerformerBridge [IdleBreath]: stopped '%s'."),
			*IdleBreathingMontage->GetName());
	}
}

void UGodfreyPerformerAnimationBridgeComponent::TryStartIdleBreathingMontage()
{
	if (!IdleBreathingMontage)
	{
		UE_LOG(LogGodfreyPerformance, Verbose, TEXT("GodfreyPerformerBridge [IdleBreath]: no IdleBreathingMontage assigned."));
		return;
	}
	// Always go through PlayMontageIfPossible so AS_*_EyeFixed remapping applies.
	PlayMontageIfPossible(IdleBreathingMontage, TEXT("IdleBreath"),
		FMath::Max(0.05f, IdleBreathingIntensity), !bDeduplicateActiveMontagePlays, true);
}

void UGodfreyPerformerAnimationBridgeComponent::PlaySpeakingIdleInternal(const bool bRestartIfAlreadyPlaying)
{
	ClearSpeakingIdleChainTimer();
	// R14: shuffled speaking-pool one-shots with soft blend — never section-loop CalmExplanation alone.
	if (UAnimMontage* const PoolMontage = PickSpeakingMontageFromPool(TEXT("SpeakingIdle")))
	{
		PlayMontageIfPossible(PoolMontage, TEXT("SpeakingIdle"), SpeakingMotionIntensity, bRestartIfAlreadyPlaying,
			/*bLoop=*/false, /*bChainAsHold=*/true, /*bSoftSlotReplace=*/true);
		if (bIsSpeaking)
		{
			ScheduleSpeakingIdleEarlyChainAdvance();
		}
		return;
	}

	const bool bLoop = bLoopSpeakingIdleMontage && SpeakingIdleMontage != nullptr;
	PlayMontageIfPossible(SpeakingIdleMontage, TEXT("SpeakingIdle"), SpeakingMotionIntensity, bRestartIfAlreadyPlaying, bLoop);
}

void UGodfreyPerformerAnimationBridgeComponent::BindSpeakingIdleMontageEndDelegate(UAnimInstance* AnimInst,
	UAnimMontage* PlayMontage)
{
	if (!AnimInst || !PlayMontage)
	{
		return;
	}

	SpeakingIdleMontageEndedDelegate.BindUObject(this, &UGodfreyPerformerAnimationBridgeComponent::OnSpeakingIdleMontageEnded);
	AnimInst->Montage_SetEndDelegate(SpeakingIdleMontageEndedDelegate, PlayMontage);
}

void UGodfreyPerformerAnimationBridgeComponent::OnSpeakingIdleMontageEnded(UAnimMontage* EndedMontage, const bool bInterrupted)
{
	if (bInterrupted)
	{
		return;
	}

	if (EndedMontage && EndedMontage == ActiveTravelMontage)
	{
		ActiveTravelMontage = nullptr;
		bTravelRootMotionActive = false;
		SetAnimInstanceIgnoreRootMotion(true);
		UE_LOG(LogGodfreyPerformance, Log,
			TEXT("GodfreyPerformerBridge: travel montage '%s' ended — restoring planted stance."),
			*EndedMontage->GetName());
		if (bIsSpeaking || bPostSpeechSpeakingBodyHold
			|| (PerformerState && PerformerState->IsInDialog()))
		{
			EnsurePlantedStancePlaying();
		}
		else if (bDriveExhibitionPresenceMontages)
		{
			PlaySeaIdleLoop();
			return;
		}
	}

	if (bPostSpeechSpeakingBodyHold)
	{
		UE_LOG(LogGodfreyPerformance, Log,
			TEXT("GodfreyPerformerBridge [PostSpeech]: speaking body '%s' ended during hold — blend to Listening* now."),
			EndedMontage ? *EndedMontage->GetName() : TEXT("(null)"));
		FinishPostSpeechSpeakingHold();
		return;
	}

	if (bIsSpeaking)
	{
		UE_LOG(LogGodfreyPerformance, Log,
			TEXT("GodfreyPerformerBridge [SpeakingIdle]: montage '%s' ended while still speaking — next speaking-pool AS."),
			EndedMontage ? *EndedMontage->GetName() : TEXT("(null)"));
		PlaySpeakingIdleInternal(true);
		return;
	}

	if (bDialogGreetingHoldActive)
	{
		bDialogGreetingHoldActive = false;
		UE_LOG(LogGodfreyPerformance, Log,
			TEXT("GodfreyPerformerBridge [DialogGreeting]: greeting hold '%s' finished — advancing to listening pool."),
			EndedMontage ? *EndedMontage->GetName() : TEXT("(null)"));
	}

	// Dialog idle hold: one-shot ended — advance to next shuffled listening-pool AS (R9).
	if (bHoldingCameraFocusWhileAwaitingReply && bIsListening && !bIsSpeaking)
	{
		const bool bAwaitingBrain = PerformerState && PerformerState->IsAwaitingBrainReply();
		UE_LOG(LogGodfreyPerformance, Log,
			TEXT("GodfreyPerformerBridge [AwaitHold]: montage '%s' ended while awaiting %s — next listening-pool AS."),
			EndedMontage ? *EndedMontage->GetName() : TEXT("(null)"),
			bAwaitingBrain ? TEXT("brain") : TEXT("visitor reply"));
		PlayAwaitingConversationHoldMontage(
			bAwaitingBrain ? TEXT("AwaitBrainListening") : TEXT("AwaitReplyNeutral"),
			true);
		return;
	}

	if (PerformerState && PerformerState->IsInDialog() && bIsThinking && !bIsSpeaking)
	{
		UE_LOG(LogGodfreyPerformance, Log,
			TEXT("GodfreyPerformerBridge [ConversingIdle]: montage '%s' ended while thinking — next listening-pool AS."),
			EndedMontage ? *EndedMontage->GetName() : TEXT("(null)"));
		PlayAwaitingConversationHoldMontage(TEXT("ConversingIdle"), true);
		return;
	}

	// R13 fallback: if early chain missed (timer cancelled / short clip), advance after natural end.
	// Prefer ScheduleSeaIdleEarlyChainAdvance — ending first lets DefaultSlot weight hit 0 → MetaHuman RefPose (A-pose).
	if (PerformerState
		&& PerformerState->GetExhibitionPresence() == EGodfreyExhibitionPresence::SeaIdle
		&& !PerformerState->IsInDialog()
		&& !bIsSpeaking)
	{
		if (ActiveSpeakingIdlePlayMontage && ActiveSpeakingIdlePlayMontage != EndedMontage
			&& IsSeaIdleChainActive())
		{
			UE_LOG(LogGodfreyPerformance, Verbose,
				TEXT("GodfreyPerformerBridge [SeaIdle]: montage '%s' ended after early chain — already on next AS."),
				EndedMontage ? *EndedMontage->GetName() : TEXT("(null)"));
			return;
		}
		UE_LOG(LogGodfreyPerformance, Log,
			TEXT("GodfreyPerformerBridge [SeaIdle]: montage '%s' ended without early chain — advancing (RefPose risk)."),
			EndedMontage ? *EndedMontage->GetName() : TEXT("(null)"));
		AdvanceSeaIdleChain();
	}
}

void UGodfreyPerformerAnimationBridgeComponent::PlayAwaitingConversationHoldMontage(const TCHAR* ContextLabel,
	const bool bPreferListeningEnter)
{
	// R16: keep speaking body through the post-speech hold — do not snap to Listening yet.
	if (bPostSpeechSpeakingBodyHold)
	{
		UE_LOG(LogGodfreyPerformance, Log,
			TEXT("GodfreyPerformerBridge [%s]: deferred — post-speech speaking body still holding."),
			ContextLabel ? ContextLabel : TEXT("AwaitHold"));
		return;
	}

	// R15: let the once-per-encounter Greeting* hold finish before replacing with Listening*.
	if (bDialogGreetingHoldActive)
	{
		if (UAnimInstance* const AnimInst = ResolveAnimInstance(TEXT("DialogGreetingHold")))
		{
			if (ActiveSpeakingIdlePlayMontage && AnimInst->Montage_IsActive(ActiveSpeakingIdlePlayMontage))
			{
				UE_LOG(LogGodfreyPerformance, Log,
					TEXT("GodfreyPerformerBridge [%s]: deferred — dialog greeting hold still playing."),
					ContextLabel ? ContextLabel : TEXT("AwaitHold"));
				return;
			}
		}
		bDialogGreetingHoldActive = false;
	}

	UAnimMontage* Montage = nullptr;
	float PlayRate = 0.85f;
	bool bSoftOneShotHold = false;

	const bool bInDialog = PerformerState && PerformerState->IsInDialog();
	const bool bUseListeningPool = bInDialog || bPreferListeningEnter;
	// R15: first SeaIdle → dialog body hold uses Greeting* once; later holds use listening pool (R7/R9).
	if (bUseListeningPool && !bUsedFirstDialogGreetingHold)
	{
		Montage = PickDialogGreetingMontageFromPool(ContextLabel ? ContextLabel : TEXT("DialogGreeting"));
		PlayRate = 0.9f;
		bSoftOneShotHold = true;
		bUsedFirstDialogGreetingHold = true;
		bDialogGreetingHoldActive = true;
		UE_LOG(LogGodfreyPerformance, Log,
			TEXT("GodfreyPerformerBridge [%s]: first dialog hold — Greeting pool (subsequent holds use Listening pool)."),
			ContextLabel ? ContextLabel : TEXT("DialogGreeting"));
	}
	else if (bUseListeningPool)
	{
		Montage = PickListeningMontageFromPool(ContextLabel);
		PlayRate = 0.9f;
		bSoftOneShotHold = true;
	}
	else if (IdleBreathingMontage)
	{
		Montage = IdleBreathingMontage;
		PlayRate = 0.72f;
	}
	else if (SeaIdleMontage)
	{
		Montage = SeaIdleMontage;
		PlayRate = 0.72f;
	}
	else
	{
		Montage = PickListeningMontageFromPool(ContextLabel);
		PlayRate = 0.9f;
		bSoftOneShotHold = true;
	}

	if (!Montage)
	{
		UE_LOG(LogGodfreyPerformance, Warning,
			TEXT("GodfreyPerformerBridge [%s]: no ListeningEnter/IdleBreathing/SeaIdle montage for await hold."),
			ContextLabel ? ContextLabel : TEXT("AwaitHold"));
		return;
	}

	// R9/R15: dialog holds are one-shots chained with soft blend — never section-loop the same AS.
	if (bSoftOneShotHold)
	{
		PlayMontageIfPossible(Montage, ContextLabel, PlayRate, true, /*bLoop=*/false, /*bChainAsHold=*/true,
			/*bSoftSlotReplace=*/true);
		ScheduleDialogIdleEarlyChainAdvance();
	}
	else
	{
		PlayMontageIfPossible(Montage, ContextLabel, PlayRate, true, true);
	}
}

void UGodfreyPerformerAnimationBridgeComponent::MaintainAwaitingConversationHoldMontage()
{
	if (bOperatorPerformanceHold)
	{
		return;
	}
	if (!bEnableBodyMontages || !bHoldingCameraFocusWhileAwaitingReply || !bIsListening || bIsSpeaking
		|| !IsValid(TargetSkeletalMesh))
	{
		return;
	}

	UAnimInstance* const AnimInst = TargetSkeletalMesh->GetAnimInstance();
	if (!AnimInst)
	{
		return;
	}

	UAnimMontage* const HoldMontage = ActiveSpeakingIdlePlayMontage.Get();
	if (HoldMontage && AnimInst->Montage_IsActive(HoldMontage))
	{
		return;
	}

	const bool bAwaitingBrain = PerformerState && PerformerState->IsAwaitingBrainReply();
	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("GodfreyPerformerBridge [AwaitHold]: hold inactive while awaiting %s — next listening-pool AS."),
		bAwaitingBrain ? TEXT("brain") : TEXT("visitor reply"));
	PlayAwaitingConversationHoldMontage(
		bAwaitingBrain ? TEXT("AwaitBrainListening") : TEXT("AwaitReplyNeutral"),
		true);
}

bool UGodfreyPerformerAnimationBridgeComponent::ShouldSuppressPresenceOwnedBodyCues() const
{
	if (!bDriveExhibitionPresenceMontages || !PerformerState)
	{
		return false;
	}
	const EGodfreyExhibitionPresence Presence = PerformerState->GetExhibitionPresence();
	return Presence == EGodfreyExhibitionPresence::SeaIdle
		|| Presence == EGodfreyExhibitionPresence::Engaging
		|| Presence == EGodfreyExhibitionPresence::Farewell;
}

void UGodfreyPerformerAnimationBridgeComponent::ClearMontageSlotIfStemEquals(TObjectPtr<UAnimMontage>& Slot,
	const TCHAR* Stem)
{
	if (!Slot || !Stem || !*Stem)
	{
		return;
	}
	const FString SlotStem = NormalizePerformanceCueId(Slot->GetName());
	if (SlotStem.Equals(Stem, ESearchCase::IgnoreCase))
	{
		Slot = nullptr;
	}
}

void UGodfreyPerformerAnimationBridgeComponent::MaintainSpeakingIdleMontage()
{
	if (bOperatorPerformanceHold)
	{
		return;
	}
	if (!bEnableBodyMontages || !bIsSpeaking || !IsValid(TargetSkeletalMesh))
	{
		return;
	}

	if (const UWorld* World = GetWorld())
	{
		if (World->GetTimeSeconds() < SuppressSpeakingIdleUntilWorldTime)
		{
			return;
		}
	}

	UAnimInstance* const AnimInst = TargetSkeletalMesh->GetAnimInstance();
	if (!AnimInst)
	{
		return;
	}

	UAnimMontage* const PlayMontage = ActiveSpeakingIdlePlayMontage.Get();
	if (PlayMontage && AnimInst->Montage_IsActive(PlayMontage))
	{
		// R14: one-shot pool chain advances on montage end — do not rewind / section-loop.
		return;
	}

	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("GodfreyPerformerBridge [SpeakingMaintain]: speaking idle inactive while bIsSpeaking — next speaking-pool AS."));
	PlaySpeakingIdleInternal(true);
}

void UGodfreyPerformerAnimationBridgeComponent::EnsureDefaultListeningPool()
{
	if (ListeningWhileVisitorSpeaksPool.Num() > 0)
	{
		return;
	}
	ListeningWhileVisitorSpeaksPool = {
		TEXT("ListeningAttentive_01"),
		TEXT("ListeningCurious_01"),
		TEXT("ListeningConcerned_01"),
		TEXT("ListeningNodding_01"),
	};
}

bool UGodfreyPerformerAnimationBridgeComponent::IsDialogIdleHoldContext(const TCHAR* ContextLabel)
{
	if (!ContextLabel || !*ContextLabel)
	{
		return false;
	}
	const FString C(ContextLabel);
	return C.Contains(TEXT("Await"), ESearchCase::IgnoreCase)
		|| C.Contains(TEXT("ConversingIdle"), ESearchCase::IgnoreCase)
		|| C.Contains(TEXT("DialogIdle"), ESearchCase::IgnoreCase)
		|| C.Contains(TEXT("PostSpeechSettle"), ESearchCase::IgnoreCase)
		|| C.Equals(TEXT("Listening"), ESearchCase::IgnoreCase);
}

bool UGodfreyPerformerAnimationBridgeComponent::IsSeaIdleHoldContext(const TCHAR* ContextLabel)
{
	if (!ContextLabel || !*ContextLabel)
	{
		return false;
	}
	return FString(ContextLabel).Equals(TEXT("SeaIdle"), ESearchCase::IgnoreCase);
}

bool UGodfreyPerformerAnimationBridgeComponent::IsSeaIdleChainActive() const
{
	const UAnimMontage* const Active = ActiveSpeakingIdlePlayMontage.Get();
	if (!Active)
	{
		return false;
	}
	if (const UAnimInstance* const AnimInst = ResolveAnimInstance(TEXT("SeaIdle")))
	{
		return AnimInst->Montage_IsActive(Active);
	}
	return false;
}

void UGodfreyPerformerAnimationBridgeComponent::EnsureDefaultSeaIdlePool()
{
	if (SeaIdleExhibitionPool.Num() > 0)
	{
		return;
	}
	// Calm exhibition hold: look-to-sea variants + quiet standing/hands — never listening-pool AS (R7).
	SeaIdleExhibitionPool = {
		TEXT("IdleLookingToSea_01"),
		TEXT("IdleLookingToSea_02"),
		TEXT("IdleStanding_01"),
		TEXT("IdleWeightShift_01"),
		TEXT("IdleRockingOnFeet_01"),
		TEXT("HandsBehindBack_01"),
		TEXT("HandsClasped_01"),
	};
}

void UGodfreyPerformerAnimationBridgeComponent::ReshuffleSeaIdlePoolOrder()
{
	EnsureDefaultSeaIdlePool();

	TArray<FString> Clean;
	Clean.Reserve(SeaIdleExhibitionPool.Num());
	for (const FString& Stem : SeaIdleExhibitionPool)
	{
		FString S = Stem.TrimStartAndEnd();
		if (S.StartsWith(TEXT("AS_"), ESearchCase::IgnoreCase))
		{
			S.RightChopInline(3);
		}
		if (!S.IsEmpty())
		{
			Clean.AddUnique(S);
		}
	}
	if (Clean.Num() == 0)
	{
		ShuffledSeaIdlePoolOrder.Reset();
		NextSeaIdlePoolIndex = 0;
		return;
	}

	for (int32 i = Clean.Num() - 1; i > 0; --i)
	{
		const int32 j = FMath::RandRange(0, i);
		Clean.Swap(i, j);
	}

	if (Clean.Num() > 1 && !LastSeaIdlePoolStem.IsEmpty()
		&& Clean[0].Equals(LastSeaIdlePoolStem, ESearchCase::IgnoreCase))
	{
		Clean.Swap(0, Clean.Num() - 1);
	}

	ShuffledSeaIdlePoolOrder = MoveTemp(Clean);
	NextSeaIdlePoolIndex = 0;

	FString OrderLog;
	for (int32 i = 0; i < ShuffledSeaIdlePoolOrder.Num(); ++i)
	{
		if (i > 0)
		{
			OrderLog += TEXT(" -> ");
		}
		OrderLog += ShuffledSeaIdlePoolOrder[i];
	}
	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("GodfreyPerformerBridge: reshuffled sea-idle deck (%d): %s"),
		ShuffledSeaIdlePoolOrder.Num(),
		*OrderLog);
}

FString UGodfreyPerformerAnimationBridgeComponent::TakeNextSeaIdlePoolStem()
{
	if (NextSeaIdlePoolIndex >= ShuffledSeaIdlePoolOrder.Num())
	{
		ReshuffleSeaIdlePoolOrder();
	}
	if (ShuffledSeaIdlePoolOrder.Num() == 0)
	{
		return FString();
	}
	const FString Stem = ShuffledSeaIdlePoolOrder[NextSeaIdlePoolIndex++];
	LastSeaIdlePoolStem = Stem;
	return Stem;
}

UAnimMontage* UGodfreyPerformerAnimationBridgeComponent::PickSeaIdleMontageFromPool(const TCHAR* ContextLabel)
{
	const FString ChosenStem = TakeNextSeaIdlePoolStem();
	if (ChosenStem.IsEmpty())
	{
		return SeaIdleMontage;
	}

	if (UAnimSequence* Seq = PreferEyeFixedSequence(LoadLibrarySequenceByStem(ChosenStem)))
	{
		UAnimMontage* const Built = MakeOrGetPlaceholderMontage(
			Seq, ContextLabel ? ContextLabel : TEXT("SeaIdle"), 1,
			UGodfreyBodyAnimInstance::DefaultBodyMontageSlotName);
		UE_LOG(LogGodfreyPerformance, Log,
			TEXT("GodfreyPerformerBridge [%s]: sea-idle pool next '%s' (deck %d/%d, EyeFixed prefer=%d)."),
			ContextLabel ? ContextLabel : TEXT("SeaIdle"),
			*ChosenStem,
			NextSeaIdlePoolIndex,
			ShuffledSeaIdlePoolOrder.Num(),
			bPreferEyeFixedLibraryVariants ? 1 : 0);
		return Built ? Built : SeaIdleMontage.Get();
	}

	UE_LOG(LogGodfreyPerformance, Warning,
		TEXT("GodfreyPerformerBridge [%s]: sea-idle pool stem '%s' missing — fallback SeaIdle montage."),
		ContextLabel ? ContextLabel : TEXT("SeaIdle"),
		*ChosenStem);
	return SeaIdleMontage.Get();
}

void UGodfreyPerformerAnimationBridgeComponent::ReshuffleListeningPoolOrder()
{
	EnsureDefaultListeningPool();

	TArray<FString> Clean;
	Clean.Reserve(ListeningWhileVisitorSpeaksPool.Num());
	for (const FString& Stem : ListeningWhileVisitorSpeaksPool)
	{
		FString S = Stem.TrimStartAndEnd();
		if (S.StartsWith(TEXT("AS_"), ESearchCase::IgnoreCase))
		{
			S.RightChopInline(3);
		}
		if (!S.IsEmpty())
		{
			Clean.AddUnique(S);
		}
	}
	if (Clean.Num() == 0)
	{
		ShuffledListeningPoolOrder.Reset();
		NextListeningPoolIndex = 0;
		return;
	}

	// Fisher–Yates shuffle.
	for (int32 i = Clean.Num() - 1; i > 0; --i)
	{
		const int32 j = FMath::RandRange(0, i);
		Clean.Swap(i, j);
	}

	// Avoid starting a new deck on the same stem that just finished.
	if (Clean.Num() > 1 && !LastListeningPoolStem.IsEmpty()
		&& Clean[0].Equals(LastListeningPoolStem, ESearchCase::IgnoreCase))
	{
		Clean.Swap(0, Clean.Num() - 1);
	}

	ShuffledListeningPoolOrder = MoveTemp(Clean);
	NextListeningPoolIndex = 0;

	FString OrderLog;
	for (int32 i = 0; i < ShuffledListeningPoolOrder.Num(); ++i)
	{
		if (i > 0)
		{
			OrderLog += TEXT(" -> ");
		}
		OrderLog += ShuffledListeningPoolOrder[i];
	}
	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("GodfreyPerformerBridge: reshuffled listening-pool deck (%d): %s"),
		ShuffledListeningPoolOrder.Num(),
		*OrderLog);
}

FString UGodfreyPerformerAnimationBridgeComponent::TakeNextListeningPoolStem()
{
	if (NextListeningPoolIndex >= ShuffledListeningPoolOrder.Num())
	{
		ReshuffleListeningPoolOrder();
	}
	if (ShuffledListeningPoolOrder.Num() == 0)
	{
		return FString();
	}
	const FString Stem = ShuffledListeningPoolOrder[NextListeningPoolIndex++];
	LastListeningPoolStem = Stem;
	return Stem;
}

UAnimMontage* UGodfreyPerformerAnimationBridgeComponent::PickListeningMontageFromPool(const TCHAR* ContextLabel)
{
	const FString ChosenStem = TakeNextListeningPoolStem();
	if (ChosenStem.IsEmpty())
	{
		return ListeningEnterMontage;
	}

	if (UAnimSequence* Seq = PreferEyeFixedSequence(LoadLibrarySequenceByStem(ChosenStem)))
	{
		UAnimMontage* const Built = MakeOrGetPlaceholderMontage(Seq, ContextLabel ? ContextLabel : TEXT("ListeningPool"), 1);
		UE_LOG(LogGodfreyPerformance, Log,
			TEXT("GodfreyPerformerBridge [%s]: listening pool next '%s' (deck %d/%d, EyeFixed prefer=%d)."),
			ContextLabel ? ContextLabel : TEXT("ListeningPool"),
			*ChosenStem,
			NextListeningPoolIndex,
			ShuffledListeningPoolOrder.Num(),
			bPreferEyeFixedLibraryVariants ? 1 : 0);
		return Built ? Built : ListeningEnterMontage.Get();
	}

	UE_LOG(LogGodfreyPerformance, Warning,
		TEXT("GodfreyPerformerBridge [%s]: listening pool stem '%s' missing — fallback ListeningEnter."),
		ContextLabel ? ContextLabel : TEXT("ListeningPool"),
		*ChosenStem);
	return ListeningEnterMontage.Get();
}

void UGodfreyPerformerAnimationBridgeComponent::EnsureDefaultSpeakingPool()
{
	if (SpeakingIdlePool.Num() > 0)
	{
		return;
	}
	// Expansive speaking body: Describing* + SpeakingDescribe*/Explain* + calm/emphasis (R14).
	SpeakingIdlePool = {
		TEXT("SpeakingCalmExplanation_01"),
		TEXT("SpeakingGentleEmphasis_01"),
		TEXT("SpeakingDescribeDistance_01"),
		TEXT("SpeakingDescribeSequence_01"),
		TEXT("SpeakingDescribeSize_01"),
		TEXT("SpeakingExplainDanger_01"),
		TEXT("DescribingTheGeorgette_01"),
		TEXT("DescribingWereYouAfraid_01"),
		TEXT("DescribingWereYouAfraid_02"),
		TEXT("DescribingWhatGraceBusselWasLike_01"),
		TEXT("DescribingWhatHappenedThatNight_01"),
		TEXT("DescribingWhatVisitorsShouldRemember_01"),
		TEXT("DescribingWhatYouWouldDoDifferently_01"),
	};
}

void UGodfreyPerformerAnimationBridgeComponent::ReshuffleSpeakingPoolOrder()
{
	EnsureDefaultSpeakingPool();

	TArray<FString> Clean;
	Clean.Reserve(SpeakingIdlePool.Num());
	for (const FString& Stem : SpeakingIdlePool)
	{
		FString S = Stem.TrimStartAndEnd();
		if (S.StartsWith(TEXT("AS_"), ESearchCase::IgnoreCase))
		{
			S.RightChopInline(3);
		}
		if (!S.IsEmpty())
		{
			Clean.AddUnique(S);
		}
	}
	if (Clean.Num() == 0)
	{
		ShuffledSpeakingPoolOrder.Reset();
		NextSpeakingPoolIndex = 0;
		return;
	}

	for (int32 i = Clean.Num() - 1; i > 0; --i)
	{
		const int32 j = FMath::RandRange(0, i);
		Clean.Swap(i, j);
	}

	if (Clean.Num() > 1 && !LastSpeakingPoolStem.IsEmpty()
		&& Clean[0].Equals(LastSpeakingPoolStem, ESearchCase::IgnoreCase))
	{
		Clean.Swap(0, Clean.Num() - 1);
	}

	ShuffledSpeakingPoolOrder = MoveTemp(Clean);
	NextSpeakingPoolIndex = 0;

	FString OrderLog;
	for (int32 i = 0; i < ShuffledSpeakingPoolOrder.Num(); ++i)
	{
		if (i > 0)
		{
			OrderLog += TEXT(" -> ");
		}
		OrderLog += ShuffledSpeakingPoolOrder[i];
	}
	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("GodfreyPerformerBridge: reshuffled speaking-pool deck (%d): %s"),
		ShuffledSpeakingPoolOrder.Num(),
		*OrderLog);
}

FString UGodfreyPerformerAnimationBridgeComponent::TakeNextSpeakingPoolStem()
{
	if (NextSpeakingPoolIndex >= ShuffledSpeakingPoolOrder.Num())
	{
		ReshuffleSpeakingPoolOrder();
	}
	if (ShuffledSpeakingPoolOrder.Num() == 0)
	{
		return FString();
	}
	const FString Stem = ShuffledSpeakingPoolOrder[NextSpeakingPoolIndex++];
	LastSpeakingPoolStem = Stem;
	return Stem;
}

UAnimMontage* UGodfreyPerformerAnimationBridgeComponent::PickSpeakingMontageFromPool(const TCHAR* ContextLabel)
{
	const FString ChosenStem = TakeNextSpeakingPoolStem();
	if (ChosenStem.IsEmpty())
	{
		return SpeakingIdleMontage;
	}

	if (UAnimSequence* Seq = PreferEyeFixedSequence(LoadLibrarySequenceByStem(ChosenStem)))
	{
		UAnimMontage* const Built = MakeOrGetPlaceholderMontage(Seq, ContextLabel ? ContextLabel : TEXT("SpeakingPool"), 1);
		UE_LOG(LogGodfreyPerformance, Log,
			TEXT("GodfreyPerformerBridge [%s]: speaking pool next '%s' (deck %d/%d, EyeFixed prefer=%d)."),
			ContextLabel ? ContextLabel : TEXT("SpeakingPool"),
			*ChosenStem,
			NextSpeakingPoolIndex,
			ShuffledSpeakingPoolOrder.Num(),
			bPreferEyeFixedLibraryVariants ? 1 : 0);
		return Built ? Built : SpeakingIdleMontage.Get();
	}

	UE_LOG(LogGodfreyPerformance, Warning,
		TEXT("GodfreyPerformerBridge [%s]: speaking pool stem '%s' missing — fallback SpeakingIdle montage."),
		ContextLabel ? ContextLabel : TEXT("SpeakingPool"),
		*ChosenStem);
	return SpeakingIdleMontage.Get();
}

bool UGodfreyPerformerAnimationBridgeComponent::IsSpeakingPoolActive() const
{
	return SpeakingIdlePool.Num() > 0 || ShuffledSpeakingPoolOrder.Num() > 0;
}

void UGodfreyPerformerAnimationBridgeComponent::EnsureDefaultDialogGreetingPool()
{
	if (DialogGreetingPool.Num() > 0)
	{
		return;
	}
	// First dialog interaction (R15). TurnToVisitor excluded — camera already frontal (see bSkipEngageTurnMontage).
	DialogGreetingPool = {
		TEXT("GreetingNod_01"),
		TEXT("GreetingSmallSmile_01"),
		TEXT("GreetingHaveASeat_01"),
		TEXT("GreetingWelcome_01"),
	};
}

void UGodfreyPerformerAnimationBridgeComponent::ReshuffleDialogGreetingPoolOrder()
{
	EnsureDefaultDialogGreetingPool();

	TArray<FString> Clean;
	Clean.Reserve(DialogGreetingPool.Num());
	for (const FString& Stem : DialogGreetingPool)
	{
		FString S = Stem.TrimStartAndEnd();
		if (S.StartsWith(TEXT("AS_"), ESearchCase::IgnoreCase))
		{
			S.RightChopInline(3);
		}
		if (!S.IsEmpty())
		{
			Clean.AddUnique(S);
		}
	}
	if (Clean.Num() == 0)
	{
		ShuffledDialogGreetingPoolOrder.Reset();
		NextDialogGreetingPoolIndex = 0;
		return;
	}

	for (int32 i = Clean.Num() - 1; i > 0; --i)
	{
		const int32 j = FMath::RandRange(0, i);
		Clean.Swap(i, j);
	}

	if (Clean.Num() > 1 && !LastDialogGreetingStem.IsEmpty()
		&& Clean[0].Equals(LastDialogGreetingStem, ESearchCase::IgnoreCase))
	{
		Clean.Swap(0, Clean.Num() - 1);
	}

	ShuffledDialogGreetingPoolOrder = MoveTemp(Clean);
	NextDialogGreetingPoolIndex = 0;

	FString OrderLog;
	for (int32 i = 0; i < ShuffledDialogGreetingPoolOrder.Num(); ++i)
	{
		if (i > 0)
		{
			OrderLog += TEXT(" -> ");
		}
		OrderLog += ShuffledDialogGreetingPoolOrder[i];
	}
	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("GodfreyPerformerBridge: reshuffled dialog-greeting deck (%d): %s"),
		ShuffledDialogGreetingPoolOrder.Num(),
		*OrderLog);
}

FString UGodfreyPerformerAnimationBridgeComponent::TakeNextDialogGreetingPoolStem()
{
	if (NextDialogGreetingPoolIndex >= ShuffledDialogGreetingPoolOrder.Num())
	{
		ReshuffleDialogGreetingPoolOrder();
	}
	if (ShuffledDialogGreetingPoolOrder.Num() == 0)
	{
		return FString();
	}
	const FString Stem = ShuffledDialogGreetingPoolOrder[NextDialogGreetingPoolIndex++];
	LastDialogGreetingStem = Stem;
	return Stem;
}

UAnimMontage* UGodfreyPerformerAnimationBridgeComponent::PickDialogGreetingMontageFromPool(const TCHAR* ContextLabel)
{
	const FString ChosenStem = TakeNextDialogGreetingPoolStem();
	if (ChosenStem.IsEmpty())
	{
		return ListeningEnterMontage;
	}

	if (UAnimSequence* Seq = PreferEyeFixedSequence(LoadLibrarySequenceByStem(ChosenStem)))
	{
		UAnimMontage* const Built = MakeOrGetPlaceholderMontage(Seq, ContextLabel ? ContextLabel : TEXT("DialogGreeting"), 1);
		UE_LOG(LogGodfreyPerformance, Log,
			TEXT("GodfreyPerformerBridge [%s]: dialog-greeting pool next '%s' (deck %d/%d, EyeFixed prefer=%d)."),
			ContextLabel ? ContextLabel : TEXT("DialogGreeting"),
			*ChosenStem,
			NextDialogGreetingPoolIndex,
			ShuffledDialogGreetingPoolOrder.Num(),
			bPreferEyeFixedLibraryVariants ? 1 : 0);
		return Built ? Built : ListeningEnterMontage.Get();
	}

	UE_LOG(LogGodfreyPerformance, Warning,
		TEXT("GodfreyPerformerBridge [%s]: dialog-greeting stem '%s' missing — fallback ListeningEnter."),
		ContextLabel ? ContextLabel : TEXT("DialogGreeting"),
		*ChosenStem);
	return ListeningEnterMontage.Get();
}

void UGodfreyPerformerAnimationBridgeComponent::ResetFirstDialogGreetingHold()
{
	bUsedFirstDialogGreetingHold = false;
	bDialogGreetingHoldActive = false;
	NextDialogGreetingPoolIndex = 0;
	ShuffledDialogGreetingPoolOrder.Reset();
}

void UGodfreyPerformerAnimationBridgeComponent::PlayListeningBehaviour()
{
	UAnimMontage* const Montage = PickListeningMontageFromPool(TEXT("Listening"));
	PlayMontageIfPossible(Montage, TEXT("Listening"), 1.f, !bDeduplicateActiveMontagePlays);
}

void UGodfreyPerformerAnimationBridgeComponent::PlayThinkingBehaviour()
{
	PlayMontageIfPossible(ThinkingMontage, TEXT("Thinking"), 1.f, !bDeduplicateActiveMontagePlays);
}

void UGodfreyPerformerAnimationBridgeComponent::PlaySpeakingStartBehaviour()
{
	PlayMontageIfPossible(SpeakingStartMontage, TEXT("SpeakingStart"), SpeakingMotionIntensity, true);
}

void UGodfreyPerformerAnimationBridgeComponent::PlaySpeakingIdleBehaviour()
{
	PlaySpeakingIdleInternal(true);
}

void UGodfreyPerformerAnimationBridgeComponent::PlayEmphasisBehaviour()
{
	PlayMontageIfPossible(EmphasisMontage, TEXT("Emphasis"), 1.f, true);
}

void UGodfreyPerformerAnimationBridgeComponent::PlayAmusedBehaviour()
{
	PlayMontageIfPossible(AmusedMontage, TEXT("Amused"), 1.f, true);
}

void UGodfreyPerformerAnimationBridgeComponent::PlaySeriousBehaviour()
{
	PlayMontageIfPossible(SeriousMontage, TEXT("Serious"), 1.f, true);
}

void UGodfreyPerformerAnimationBridgeComponent::PlayReturnToIdleBehaviour()
{
	PlayMontageIfPossible(ReturnToIdleMontage, TEXT("ReturnToIdle"), 1.f, !bDeduplicateActiveMontagePlays);
}

void UGodfreyPerformerAnimationBridgeComponent::StopSpeakingBehaviour()
{
	ClearSpeakingIdleChainTimer();
	if (!IsValid(TargetSkeletalMesh))
	{
		UE_LOG(LogGodfreyPerformance, Warning, TEXT("GodfreyPerformerBridge [StopSpeaking]: TargetSkeletalMesh is not set."));
		return;
	}
	UAnimInstance* const AnimInst = TargetSkeletalMesh->GetAnimInstance();
	if (!AnimInst)
	{
		UE_LOG(LogGodfreyPerformance, Warning, TEXT("GodfreyPerformerBridge [StopSpeaking]: no AnimInstance on mesh '%s'."),
			*TargetSkeletalMesh->GetName());
		return;
	}

	auto StopMontageExact = [&](UAnimMontage* Montage, const TCHAR* Label)
	{
		if (!Montage)
		{
			return;
		}
		if (AnimInst->Montage_IsActive(Montage))
		{
			AnimInst->Montage_Stop(GetSpeakingIdleMontageBlendOut(), Montage);
			UE_LOG(LogGodfreyPerformance, Log, TEXT("GodfreyPerformerBridge [StopSpeaking]: stopped %s montage '%s'."),
				Label, *Montage->GetName());
		}
	};

	auto StopMontageIfActive = [&](UAnimMontage* Montage, const TCHAR* Label)
	{
		if (!Montage)
		{
			return;
		}
		StopMontageExact(Montage, Label);
		UAnimMontage* const Resolved = ResolveMontageForBodySlot(Montage, TEXT("StopSpeaking"));
		if (Resolved && Resolved != Montage)
		{
			StopMontageExact(Resolved, Label);
		}
	};

	auto StopLoopedCacheFor = [&](UAnimMontage* Key, const TCHAR* Label)
	{
		if (!Key)
		{
			return;
		}
		if (TObjectPtr<UAnimMontage>* Looped = LoopedBodySlotMontages.Find(Key))
		{
			StopMontageExact(Looped->Get(), Label);
		}
		// EyeFixed remap creates a different key in the loop cache than the BP slot montage.
		UAnimMontage* const Remapped = RemapMontageToEyeFixedVariant(Key);
		if (Remapped && Remapped != Key)
		{
			StopMontageExact(Remapped, Label);
			if (TObjectPtr<UAnimMontage>* LoopedR = LoopedBodySlotMontages.Find(Remapped))
			{
				StopMontageExact(LoopedR->Get(), Label);
			}
		}
	};

	// Critical: after EyeFixed remap, the playing instance is often a dynamic montage stored here,
	// not SpeakingIdleMontage itself — stop that first or speech body motion continues past audio.
	StopMontageExact(ActiveSpeakingIdlePlayMontage.Get(), TEXT("active-idle"));

	StopMontageIfActive(SpeakingIdleMontage, TEXT("idle"));
	StopMontageIfActive(SpeakingStartMontage, TEXT("start"));
	StopLoopedCacheFor(SpeakingIdleMontage, TEXT("idle-looped"));
	StopLoopedCacheFor(SpeakingStartMontage, TEXT("start-looped"));

	ActiveSpeakingIdlePlayMontage = nullptr;
	SpeakingIdleMontageCycleSeconds = 0.f;
	SpeakingIdleMontageWallCycleSeconds = 0.f;
	SpeakingIdleCycleStartWorldTime = -1.0;

	if (UGodfreyDiagnosticsSubsystem* Diag = UGodfreyDiagnosticsSubsystem::Get(this))
	{
		Diag->MarkStageForCurrent(EGodfreyUtteranceStage::BodyAnimEnded);
		Diag->SetCurrentAnimationName(TEXT("(none)"));
	}
}

void UGodfreyPerformerAnimationBridgeComponent::HandleListeningStarted()
{
	const EGodfreyPerformanceState PerformanceStateNow =
		PerformerState ? PerformerState->GetPerformanceState() : EGodfreyPerformanceState::Idle;
	const bool bPerformanceSpeaking = PerformanceStateNow == EGodfreyPerformanceState::Speaking;

	// R16: after audio ends we keep the speaking body briefly — do not hard-cut to Listening*.
	if (!bPostSpeechSpeakingBodyHold)
	{
		StopSpeakingBehaviour();
	}
	StopIdleBreathingMontageIfActive();
	bIsListening = true;
	bIsThinking = false;
	bIsSpeaking = false;
	RefreshMirroredPerformanceState();
	UE_LOG(LogGodfreyPerformance, Log, TEXT("GodfreyPerformerBridge: behaviour Listening (flags updated)."));
	LogOrientationSnapshot(this, TEXT("HandleListeningStarted"), CurrentAttentionTarget.Get());
	OnBridgeListening.Broadcast();
	const bool bAwaitingBrain =
		PerformerState && PerformerState->IsAwaitingBrainReply();
	const bool bConversingWait =
		PerformerState
		&& PerformerState->IsInDialog()
		&& !bPerformanceSpeaking;
	const bool bInPostSpeechSettle =
		GetWorld() && GetWorld()->GetTimeSeconds() < PostSpeechSettleUntilWorldTime;

	auto ApplyAwaitCamera = [this](const TCHAR* Reason)
	{
		SnapOwnerYawToExhibitionFacing(this, Reason);
		if (!bHoldingCameraFocusWhileAwaitingReply)
		{
			bHoldingCameraFocusWhileAwaitingReply = true;
			SavedAttentionTargetForAwaitingReply = CurrentAttentionTarget;
			bSavedAttentionFollowForAwaitingReply = bEnableAttentionTargetFollow;
			SavedAttentionOffsetStrengthForAwaitingReply = AttentionOffsetStrength;
			SavedAttentionInterpSpeedForAwaitingReply = AttentionInterpSpeed;
		}
		if (AActor* FocusTarget = ResolvePrimaryViewTarget(this))
		{
			CurrentAttentionTarget = FocusTarget;
		}
		bEnableAttentionTargetFollow = true;
		AttentionOffsetStrength = FMath::Max(AttentionOffsetStrength, SpeakingCameraAttentionOffsetStrength);
		AttentionInterpSpeed = FMath::Max(2.5f, AttentionInterpSpeed);
	};

	if (bPostSpeechSpeakingBodyHold)
	{
		ApplyAwaitCamera(TEXT("PostSpeechSpeakingHold"));
		UE_LOG(LogGodfreyPerformance, Log,
			TEXT("GodfreyPerformerBridge [Listening]: post-speech speaking body hold active — Listening* deferred."));
		UpdatePerformerTickEnabled();
		return;
	}

	// Brain accepted a question; LLM still running — keep camera lock but play real listening montage.
	if (bAwaitingBrain && bConversingWait)
	{
		ApplyAwaitCamera(TEXT("AwaitBrainListening"));
		PlayAwaitingConversationHoldMontage(TEXT("AwaitBrainListening"), true);
		UpdatePerformerTickEnabled();
		return;
	}
	// While awaiting visitor reply, hold gaze at camera and keep a gentle listening loop active.
	if (bConversingWait)
	{
		ApplyAwaitCamera(TEXT("AwaitReplyListening"));
		// Attentive listening-pool hold while awaiting visitor reply (R7: in-dialog, never sea idle).
		if (!bPerformanceSpeaking)
		{
			PlayAwaitingConversationHoldMontage(TEXT("AwaitReplyNeutral"), true);
		}
		UpdatePerformerTickEnabled();
		return;
	}
	if (bInPostSpeechSettle)
	{
		SnapOwnerYawToExhibitionFacing(this, TEXT("PostSpeechSettleListening"));
		if (!bPerformanceSpeaking)
		{
			if (PerformerState && PerformerState->IsInDialog())
			{
				PlayAwaitingConversationHoldMontage(TEXT("PostSpeechSettle"), true);
			}
			else if (IdleBreathingMontage)
			{
				PlayMontageIfPossible(IdleBreathingMontage, TEXT("PostSpeechSettle"), 0.72f, false, true);
			}
		}
		UpdatePerformerTickEnabled();
		return;
	}
	PlayMontageIfPossible(
		PickListeningMontageFromPool(TEXT("Listening")),
		TEXT("Listening"),
		1.f,
		!bDeduplicateActiveMontagePlays,
		bConversingWait);
	UpdatePerformerTickEnabled();
}

void UGodfreyPerformerAnimationBridgeComponent::HandleThinkingStarted()
{
	if (!bPostSpeechSpeakingBodyHold)
	{
		StopSpeakingBehaviour();
	}
	StopIdleBreathingMontageIfActive();
	bIsListening = false;
	bIsThinking = true;
	bIsSpeaking = false;
	RefreshMirroredPerformanceState();
	UE_LOG(LogGodfreyPerformance, Log, TEXT("GodfreyPerformerBridge: behaviour Thinking (flags updated)."));
	LogOrientationSnapshot(this, TEXT("HandleThinkingStarted"), CurrentAttentionTarget.Get());
	OnBridgeThinking.Broadcast();
	if (bPostSpeechSpeakingBodyHold)
	{
		UE_LOG(LogGodfreyPerformance, Log,
			TEXT("GodfreyPerformerBridge [Thinking]: post-speech speaking body hold active — ConversingIdle deferred."));
		UpdatePerformerTickEnabled();
		return;
	}
	const bool bConversingWait =
		PerformerState
		&& PerformerState->IsInDialog()
		&& !bIsSpeaking;
	const bool bInPostSpeechSettle =
		GetWorld() && GetWorld()->GetTimeSeconds() < PostSpeechSettleUntilWorldTime;
	if (bConversingWait)
	{
		SnapOwnerYawToExhibitionFacing(this, TEXT("AwaitReplyThinking"));
		// R7: in-dialog thinking hold uses attentive listening pool, not sea / weight-shift idle.
		PlayAwaitingConversationHoldMontage(TEXT("ConversingIdle"), true);
		UpdatePerformerTickEnabled();
		return;
	}
	if (bInPostSpeechSettle)
	{
		SnapOwnerYawToExhibitionFacing(this, TEXT("PostSpeechSettleThinking"));
		if (PerformerState && PerformerState->IsInDialog())
		{
			PlayAwaitingConversationHoldMontage(TEXT("PostSpeechSettle"), true);
		}
		else if (IdleBreathingMontage)
		{
			PlayMontageIfPossible(IdleBreathingMontage, TEXT("PostSpeechSettle"), 0.72f, false, true);
		}
		UpdatePerformerTickEnabled();
		return;
	}
	if (ShouldSuppressPresenceOwnedBodyCues())
	{
		UE_LOG(LogGodfreyAnimation, Log,
			TEXT("[Acting] miss | t=%.3f | context=Thinking | reason=presence-owns-body"),
			GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0);
		UpdatePerformerTickEnabled();
		return;
	}
	PlayMontageIfPossible(
		ThinkingMontage,
		TEXT("Thinking"),
		1.f,
		!bDeduplicateActiveMontagePlays,
		bConversingWait);
	UpdatePerformerTickEnabled();
}

void UGodfreyPerformerAnimationBridgeComponent::HandleSpeakingStarted()
{
	ClearPostSpeechSpeakingHoldTimer();
	bPostSpeechSpeakingBodyHold = false;
	ClearSpeakingIdleChainTimer();
	ClearDialogIdleChainTimer();
	StopIdleBreathingMontageIfActive();
	bIsListening = false;
	bIsThinking = false;
	bIsSpeaking = true;
	RefreshMirroredPerformanceState();
	UE_LOG(LogGodfreyPerformance, Log, TEXT("GodfreyPerformerBridge: behaviour SpeakingStarted (flags updated)."));
	LogOrientationSnapshot(this, TEXT("HandleSpeakingStarted.BeforeFocus"), CurrentAttentionTarget.Get());
	OnBridgeSpeakingStarted.Broadcast();

	if (bAutoFocusCameraWhileSpeaking)
	{
		SavedAttentionTargetForSpeaking = CurrentAttentionTarget;
		bSavedAttentionFollowForSpeaking = bEnableAttentionTargetFollow;
		SavedAttentionOffsetStrengthForSpeaking = AttentionOffsetStrength;
		SavedAttentionInterpSpeedForSpeaking = AttentionInterpSpeed;

		if (AActor* FocusTarget = ResolvePrimaryViewTarget(this))
		{
			CurrentAttentionTarget = FocusTarget;
			UE_LOG(LogGodfreyPerformance, Log, TEXT("GodfreyPerformerBridge: speaking focus target '%s'."),
				*FocusTarget->GetName());
		}
		else
		{
			CurrentAttentionTarget = nullptr;
			UE_LOG(LogGodfreyPerformance, Warning,
				TEXT("GodfreyPerformerBridge: speaking has NO view target; camera focus disabled for debugging (no fallback)."));
		}
		bEnableAttentionTargetFollow = true;
		AttentionOffsetStrength = FMath::Clamp(SpeakingCameraAttentionOffsetStrength, 0.f, 45.f);
		AttentionInterpSpeed = FMath::Max(0.1f, SpeakingCameraAttentionInterpSpeed);
	}
	SnapOwnerYawToExhibitionFacing(this, TEXT("SpeakingStart"));
	LogOrientationSnapshot(this, TEXT("HandleSpeakingStarted.AfterSnap"), CurrentAttentionTarget.Get());

	const bool bSameStartAndIdle =
		SpeakingStartMontage != nullptr && SpeakingStartMontage == SpeakingIdleMontage;
	const bool bSkipStart =
		bPreferSpeakingIdleLoopOnly || bSameStartAndIdle || SpeakingStartMontage == nullptr;

	if (!bSkipStart)
	{
		PlayMontageIfPossible(SpeakingStartMontage, TEXT("SpeakingStart"), SpeakingMotionIntensity, true, false);
	}

	PlaySpeakingIdleInternal(!bSkipStart);
	UpdatePerformerTickEnabled();
}


void UGodfreyPerformerAnimationBridgeComponent::HandleSpeakingEnded()
{
	ClearSpeakingIdleChainTimer();
	bIsSpeaking = false;
	RefreshMirroredPerformanceState();
	UE_LOG(LogGodfreyPerformance, Log, TEXT("GodfreyPerformerBridge: behaviour SpeakingEnded."));
	LogOrientationSnapshot(this, TEXT("HandleSpeakingEnded.BeforeStop"), CurrentAttentionTarget.Get());
	OnBridgeSpeakingEnded.Broadcast();
	// R16: lipsync/audio already stopped — keep speaking body AS briefly, then soft-blend to Listening*.
	BeginPostSpeechSpeakingHold();
	if (const UWorld* World = GetWorld())
	{
		PostSpeechSettleUntilWorldTime = World->GetTimeSeconds()
			+ FMath::Max(static_cast<double>(PostSpeechSettleSeconds),
				static_cast<double>(PostSpeechSpeakingHoldSeconds) + 1.0);
	}
	if (bAutoFocusCameraWhileSpeaking)
	{
		// Do not restore pre-speech focus immediately; keep camera lock while awaiting visitor reply.
		bEnableAttentionTargetFollow = true;
		AttentionOffsetStrength = FMath::Clamp(SpeakingCameraAttentionOffsetStrength, 0.f, 45.f);
		AttentionInterpSpeed = FMath::Max(2.5f, SpeakingCameraAttentionInterpSpeed);
		if (AActor* FocusTarget = ResolvePrimaryViewTarget(this))
		{
			CurrentAttentionTarget = FocusTarget;
		}
		bHoldingCameraFocusWhileAwaitingReply = true;
	}
	SnapOwnerYawToExhibitionFacing(this, TEXT("SpeakingEnd"));
	LogOrientationSnapshot(this, TEXT("HandleSpeakingEnded.AfterSnap"), CurrentAttentionTarget.Get());
	UpdatePerformerTickEnabled();
}

void UGodfreyPerformerAnimationBridgeComponent::HandleReturnedToIdle()
{
	StopSpeakingBehaviour();
	bIsListening = false;
	bIsThinking = false;
	bIsSpeaking = false;
	GestureIntensity = GestureIntensityDefault;
	RefreshMirroredPerformanceState();
	OnBridgeReturnedToIdle.Broadcast();
	LogOrientationSnapshot(this, TEXT("HandleReturnedToIdle"), CurrentAttentionTarget.Get());

	if (bDriveExhibitionPresenceMontages && PerformerState
		&& PerformerState->GetExhibitionPresence() == EGodfreyExhibitionPresence::SeaIdle)
	{
		UE_LOG(LogGodfreyPerformance, Log,
			TEXT("GodfreyPerformerBridge: ReturnedToIdle under SeaIdle — maintaining look-to-sea loop."));
		PlaySeaIdleLoop();
		return;
	}

	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("GodfreyPerformerBridge: behaviour ReturnedToIdle -> idle micro / breathing layer may start (attention flags cleared; mood flags unchanged)."));
	if (PerformerState && PerformerState->IsInDialog())
	{
		SnapOwnerYawToExhibitionFacing(this, TEXT("ReturnedToIdleConversing"));
		PlayAwaitingConversationHoldMontage(TEXT("AwaitReplyNeutral"), true);
		UpdatePerformerTickEnabled();
		return;
	}
	PlayMontageIfPossible(ReturnToIdleMontage, TEXT("ReturnToIdle"), 1.f, !bDeduplicateActiveMontagePlays);
	TryStartIdleBreathingMontage();
}

void UGodfreyPerformerAnimationBridgeComponent::HandleEmphasisTriggered()
{
	UWorld* const World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	const bool bCooldownActive =
		World && (Now - LastEmphasisMontageWorldTimeSeconds) < static_cast<double>(GestureCooldownSeconds);
	if (bCooldownActive)
	{
		UE_LOG(LogGodfreyPerformance, Log,
			TEXT("GodfreyPerformerBridge: emphasis cooldown skip (elapsed=%.3fs, need>=%.3fs)."),
			Now - LastEmphasisMontageWorldTimeSeconds, GestureCooldownSeconds);
		if (bFireBridgeEmphasisOnCooldownSkip)
		{
			OnBridgeEmphasis.Broadcast();
		}
		return;
	}

	if (World)
	{
		LastEmphasisMontageWorldTimeSeconds = Now;
	}

	GestureIntensity = FMath::Clamp(GestureIntensity + 0.35f, 1.f, 2.5f);
	RefreshMirroredPerformanceState();
	UE_LOG(LogGodfreyPerformance, Log, TEXT("GodfreyPerformerBridge: behaviour Emphasis (GestureIntensity=%.2f)."), GestureIntensity);
	OnBridgeEmphasis.Broadcast();
	if (ShouldSuppressPresenceOwnedBodyCues())
	{
		UE_LOG(LogGodfreyAnimation, Log,
			TEXT("[Acting] miss | t=%.3f | context=Emphasis | reason=presence-owns-body"),
			GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0);
		return;
	}
	PlayMontageIfPossible(EmphasisMontage, TEXT("Emphasis"), 1.f, true);
}

void UGodfreyPerformerAnimationBridgeComponent::HandleAmusedTriggered()
{
	bIsSerious = false;
	bIsAmused = true;
	RefreshMirroredPerformanceState();
	UE_LOG(LogGodfreyPerformance, Log, TEXT("GodfreyPerformerBridge: behaviour Amused (mood flags updated)."));
	OnBridgeAmused.Broadcast();
	if (ShouldSuppressPresenceOwnedBodyCues())
	{
		UE_LOG(LogGodfreyAnimation, Log,
			TEXT("[Acting] miss | t=%.3f | context=Amused | reason=presence-owns-body"),
			GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0);
		return;
	}
	PlayMontageIfPossible(AmusedMontage, TEXT("Amused"), 1.f, true);
}

void UGodfreyPerformerAnimationBridgeComponent::HandleSeriousTriggered()
{
	bIsAmused = false;
	bIsSerious = true;
	RefreshMirroredPerformanceState();
	UE_LOG(LogGodfreyPerformance, Log, TEXT("GodfreyPerformerBridge: behaviour Serious (mood flags updated)."));
	OnBridgeSerious.Broadcast();
	if (ShouldSuppressPresenceOwnedBodyCues())
	{
		UE_LOG(LogGodfreyAnimation, Log,
			TEXT("[Acting] miss | t=%.3f | context=Serious | reason=presence-owns-body"),
			GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0);
		return;
	}
	PlayMontageIfPossible(SeriousMontage, TEXT("Serious"), 1.f, true);
}

void UGodfreyPerformerAnimationBridgeComponent::HandlePerformanceCueReceived(const FString& CueType, const FString& CueValue,
	const FString& RawCue)
{
	LastActingCueType = CueType;
	LastActingCueValue = !CueValue.IsEmpty() ? CueValue : CueType;
	LogActingCue(CueType, CueValue, RawCue);

	UE_LOG(LogGodfreyPerformance, Log, TEXT("GodfreyPerformerBridge: cue forwarded type=\"%s\" value=\"%s\"."), *CueType, *CueValue);

	if (bPlayNamedActionsFromCueBus)
	{
		const bool bNamedType = IsNamedActionCueType(CueType);
		const FString ActionId = !CueValue.IsEmpty() ? CueValue : CueType;
		const bool bFarewellAction = ActionId.Contains(TEXT("Farewell"), ESearchCase::IgnoreCase)
			|| ActionId.Contains(TEXT("goodbye"), ESearchCase::IgnoreCase);
		// Farewell presence sequence owns FarewellWave playback — avoid double-play.
		if (bFarewellAction && PerformerState
			&& PerformerState->GetExhibitionPresence() == EGodfreyExhibitionPresence::Farewell)
		{
			OnBridgeCueReceived.Broadcast(CueType, CueValue, RawCue);
			return;
		}
		// SeaIdle / Engaging / Farewell own the body chain — do not let story gestures interrupt.
		if (ShouldSuppressPresenceOwnedBodyCues()
			&& (bNamedType || LooksLikeNamedPerformanceId(ActionId))
			&& !ActionId.IsEmpty())
		{
			UE_LOG(LogGodfreyAnimation, Log,
				TEXT("[Acting] miss | t=%.3f | context=NamedAction | cueValue=%s | reason=presence-owns-body"),
				GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0,
				*ActionId);
			OnBridgeCueReceived.Broadcast(CueType, CueValue, RawCue);
			return;
		}
		if ((bNamedType || LooksLikeNamedPerformanceId(ActionId)) && !ActionId.IsEmpty())
		{
			PlayNamedPerformanceAction(ActionId, true);
		}
	}

	OnBridgeCueReceived.Broadcast(CueType, CueValue, RawCue);
}

void UGodfreyPerformerAnimationBridgeComponent::HandleSeaIdleStarted()
{
	ClearPostSpeechSpeakingHoldTimer();
	bPostSpeechSpeakingBodyHold = false;
	ClearSpeakingIdleChainTimer();
	ResetFirstDialogGreetingHold();
	if (bHoldingCameraFocusWhileAwaitingReply)
	{
		CurrentAttentionTarget = SavedAttentionTargetForAwaitingReply.Get();
		bEnableAttentionTargetFollow = bSavedAttentionFollowForAwaitingReply;
		AttentionOffsetStrength = SavedAttentionOffsetStrengthForAwaitingReply;
		AttentionInterpSpeed = SavedAttentionInterpSpeedForAwaitingReply;
		bHoldingCameraFocusWhileAwaitingReply = false;
	}
	if (!bDriveExhibitionPresenceMontages)
	{
		return;
	}
	PlaySeaIdleLoop();
}

void UGodfreyPerformerAnimationBridgeComponent::HandleEngageSequenceStarted()
{
	ClearSeaIdleChainTimer();
	if (!bDriveExhibitionPresenceMontages)
	{
		if (PerformerState)
		{
			PerformerState->NotifyEngageSequenceFinished();
		}
		return;
	}
	PlayEngageSequence();
}

void UGodfreyPerformerAnimationBridgeComponent::HandleFarewellSequenceStarted()
{
	ClearSeaIdleChainTimer();
	if (bHoldingCameraFocusWhileAwaitingReply)
	{
		CurrentAttentionTarget = SavedAttentionTargetForAwaitingReply.Get();
		bEnableAttentionTargetFollow = bSavedAttentionFollowForAwaitingReply;
		AttentionOffsetStrength = SavedAttentionOffsetStrengthForAwaitingReply;
		AttentionInterpSpeed = SavedAttentionInterpSpeedForAwaitingReply;
		bHoldingCameraFocusWhileAwaitingReply = false;
	}
	if (!bDriveExhibitionPresenceMontages)
	{
		if (PerformerState)
		{
			PerformerState->NotifyFarewellSequenceFinished();
		}
		return;
	}
	PlayFarewellSequence();
}

UAnimMontage* UGodfreyPerformerAnimationBridgeComponent::ResolvePresenceMontageSlot(TObjectPtr<UAnimMontage>& Slot,
	const TSoftObjectPtr<UAnimSequence>& SoftSeq, const TCHAR* Label, const int32 LoopCount)
{
	if (Slot)
	{
		if (bPreferEyeFixedLibraryVariants)
		{
			UAnimMontage* const Remapped = RemapMontageToEyeFixedVariant(Slot.Get());
			if (Remapped && Remapped != Slot.Get())
			{
				Slot = Remapped;
			}
		}
		return Slot.Get();
	}
	UAnimSequence* Sequence = SoftSeq.LoadSynchronous();
	if (Sequence)
	{
		Sequence = PreferEyeFixedSequence(Sequence);
	}
	else if (SoftSeq.ToSoftObjectPath().IsValid())
	{
		Sequence = LoadLibrarySequenceByStem(SoftSeq.GetAssetName());
	}
	if (!Sequence)
	{
		UE_LOG(LogGodfreyPerformance, Warning, TEXT("GodfreyPerformerBridge: presence sequence missing for %s."), Label);
		return nullptr;
	}
	const FString Stem = NormalizePerformanceCueId(Sequence->GetName());
	if (UAnimMontage* Authored = LoadLibraryMontageByStem(Stem))
	{
		Slot = Authored;
		return Authored;
	}
	Slot = MakeOrGetPlaceholderMontage(Sequence, Label, LoopCount);
	return Slot.Get();
}

void UGodfreyPerformerAnimationBridgeComponent::PlayPresenceMontageChainStep(UAnimMontage* Montage, const TCHAR* Label,
	FTimerHandle& TimerHandle, FTimerDelegate NextStep, const float FallbackSeconds)
{
	UWorld* const World = GetWorld();
	if (World)
	{
		World->GetTimerManager().ClearTimer(TimerHandle);
	}

	StopIdleBreathingMontageIfActive();
	const bool bPlayed = PlayMontageIfPossible(Montage, Label, 1.f, true, false);
	float Delay = FallbackSeconds;
	if (bPlayed && Montage)
	{
		Delay = FMath::Max(0.35f, Montage->GetPlayLength() * 0.92f);
	}
	const double WorldTimeSeconds = World ? World->GetTimeSeconds() : -1.0;
	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("GodfreyPerformerBridge [SeqChain]: t=%.3f label=%s played=%d nextDelay=%.2f montage='%s'"),
		WorldTimeSeconds,
		Label ? Label : TEXT("(none)"),
		bPlayed ? 1 : 0,
		Delay,
		Montage ? *Montage->GetName() : TEXT("(none)"));
	if (World)
	{
		World->GetTimerManager().SetTimer(TimerHandle, NextStep, Delay, false);
	}
	else
	{
		NextStep.ExecuteIfBound();
	}
}

void UGodfreyPerformerAnimationBridgeComponent::ClearSeaIdleChainTimer()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SeaIdleChainTimerHandle);
	}
}

void UGodfreyPerformerAnimationBridgeComponent::ClearSpeakingIdleChainTimer()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SpeakingIdleChainTimerHandle);
	}
}

void UGodfreyPerformerAnimationBridgeComponent::ScheduleSpeakingIdleEarlyChainAdvance()
{
	ClearSpeakingIdleChainTimer();
	UWorld* const World = GetWorld();
	if (!World || !bIsSpeaking)
	{
		return;
	}

	// Montage_Play may return timeline length (not wall). Prefer timeline / play-rate for true wall time.
	const float TimelineSeconds = FMath::Max(0.25f, SpeakingIdleMontageCycleSeconds);
	const float Rate = FMath::Max(0.05f, SpeakingMotionIntensity);
	const float WallFromTimeline = TimelineSeconds / Rate;
	const float WallSeconds = FMath::Max(WallFromTimeline, SpeakingIdleMontageWallCycleSeconds);
	const float LeadSeconds = FMath::Clamp(GetDialogIdleMontageBlendOut(), 0.35f, WallSeconds * 0.45f);
	const float DelaySeconds = FMath::Max(0.25f, WallSeconds - LeadSeconds);

	World->GetTimerManager().SetTimer(
		SpeakingIdleChainTimerHandle,
		FTimerDelegate::CreateUObject(this, &UGodfreyPerformerAnimationBridgeComponent::AdvanceSpeakingIdleChain),
		DelaySeconds,
		false);

	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("GodfreyPerformerBridge [SpeakingIdle]: early chain in %.2fs (wall=%.2fs lead=%.2fs) — soft overlap."),
		DelaySeconds, WallSeconds, LeadSeconds);
}

void UGodfreyPerformerAnimationBridgeComponent::AdvanceSpeakingIdleChain()
{
	ClearSpeakingIdleChainTimer();
	if (!bIsSpeaking)
	{
		return;
	}
	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("GodfreyPerformerBridge [SpeakingIdle]: early chain advance — next speaking-pool AS."));
	PlaySpeakingIdleInternal(true);
}

void UGodfreyPerformerAnimationBridgeComponent::ClearDialogIdleChainTimer()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DialogIdleChainTimerHandle);
	}
}

void UGodfreyPerformerAnimationBridgeComponent::ScheduleDialogIdleEarlyChainAdvance()
{
	ClearDialogIdleChainTimer();
	UWorld* const World = GetWorld();
	if (!World || bIsSpeaking || bPostSpeechSpeakingBodyHold)
	{
		return;
	}

	const bool bDialogHoldActive =
		(bHoldingCameraFocusWhileAwaitingReply && bIsListening)
		|| (PerformerState && PerformerState->IsInDialog() && bIsThinking)
		|| bDialogGreetingHoldActive;
	if (!bDialogHoldActive)
	{
		return;
	}

	// Montage_Play may return timeline length; prefer wall seconds already stored from PlayMontageIfPossible.
	const float TimelineSeconds = FMath::Max(0.25f, SpeakingIdleMontageCycleSeconds);
	constexpr float DialogHoldPlayRate = 0.9f;
	const float WallFromTimeline = TimelineSeconds / DialogHoldPlayRate;
	const float WallSeconds = FMath::Max(WallFromTimeline, SpeakingIdleMontageWallCycleSeconds);
	const float LeadSeconds = FMath::Clamp(GetDialogIdleMontageBlendOut(), 0.35f, WallSeconds * 0.45f);
	const float DelaySeconds = FMath::Max(0.25f, WallSeconds - LeadSeconds);

	World->GetTimerManager().SetTimer(
		DialogIdleChainTimerHandle,
		FTimerDelegate::CreateUObject(this, &UGodfreyPerformerAnimationBridgeComponent::AdvanceDialogIdleChain),
		DelaySeconds,
		false);

	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("GodfreyPerformerBridge [DialogIdle]: early chain in %.2fs (wall=%.2fs lead=%.2fs) — overlap avoids RefPose."),
		DelaySeconds, WallSeconds, LeadSeconds);
}

void UGodfreyPerformerAnimationBridgeComponent::AdvanceDialogIdleChain()
{
	ClearDialogIdleChainTimer();
	if (bIsSpeaking || bPostSpeechSpeakingBodyHold)
	{
		return;
	}

	if (bDialogGreetingHoldActive)
	{
		bDialogGreetingHoldActive = false;
	}

	if (bHoldingCameraFocusWhileAwaitingReply && bIsListening && !bIsSpeaking)
	{
		const bool bAwaitingBrain = PerformerState && PerformerState->IsAwaitingBrainReply();
		UE_LOG(LogGodfreyPerformance, Log,
			TEXT("GodfreyPerformerBridge [DialogIdle]: early chain advance — next listening-pool AS (awaiting %s)."),
			bAwaitingBrain ? TEXT("brain") : TEXT("visitor"));
		PlayAwaitingConversationHoldMontage(
			bAwaitingBrain ? TEXT("AwaitBrainListening") : TEXT("AwaitReplyNeutral"),
			true);
		return;
	}

	if (PerformerState && PerformerState->IsInDialog() && bIsThinking && !bIsSpeaking)
	{
		UE_LOG(LogGodfreyPerformance, Log,
			TEXT("GodfreyPerformerBridge [DialogIdle]: early chain advance — next listening-pool AS (thinking)."));
		PlayAwaitingConversationHoldMontage(TEXT("ConversingIdle"), true);
	}
}

void UGodfreyPerformerAnimationBridgeComponent::ClearPostSpeechSpeakingHoldTimer()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PostSpeechSpeakingHoldTimerHandle);
	}
}

void UGodfreyPerformerAnimationBridgeComponent::BeginPostSpeechSpeakingHold()
{
	ClearPostSpeechSpeakingHoldTimer();
	const float HoldSeconds = FMath::Max(0.0f, PostSpeechSpeakingHoldSeconds);
	if (HoldSeconds <= KINDA_SMALL_NUMBER
		|| !ActiveSpeakingIdlePlayMontage)
	{
		bPostSpeechSpeakingBodyHold = false;
		FinishPostSpeechSpeakingHold();
		return;
	}

	bPostSpeechSpeakingBodyHold = true;
	UWorld* const World = GetWorld();
	if (!World)
	{
		FinishPostSpeechSpeakingHold();
		return;
	}

	World->GetTimerManager().SetTimer(
		PostSpeechSpeakingHoldTimerHandle,
		FTimerDelegate::CreateUObject(this, &UGodfreyPerformerAnimationBridgeComponent::FinishPostSpeechSpeakingHold),
		HoldSeconds,
		false);

	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("GodfreyPerformerBridge [PostSpeech]: holding speaking body '%.2fs' after audio (montage='%s')."),
		HoldSeconds,
		*ActiveSpeakingIdlePlayMontage->GetName());
}

void UGodfreyPerformerAnimationBridgeComponent::FinishPostSpeechSpeakingHold()
{
	ClearPostSpeechSpeakingHoldTimer();
	const bool bWasHolding = bPostSpeechSpeakingBodyHold;
	bPostSpeechSpeakingBodyHold = false;
	if (!bWasHolding && !ActiveSpeakingIdlePlayMontage)
	{
		return;
	}

	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("GodfreyPerformerBridge [PostSpeech]: soft-blend speaking body -> Listening* (blendOut=%.2fs blendIn=%.2fs)."),
		GetDialogIdleMontageBlendOut(),
		GetDialogIdleMontageBlendIn());

	if (PerformerState && PerformerState->IsInDialog())
	{
		PlayAwaitingConversationHoldMontage(
			bIsThinking ? TEXT("PostSpeechConversingIdle") : TEXT("PostSpeechAwaitReply"),
			true);
	}
	else
	{
		ActiveSpeakingIdlePlayMontage = nullptr;
		SpeakingIdleMontageCycleSeconds = 0.f;
		SpeakingIdleMontageWallCycleSeconds = 0.f;
		SpeakingIdleCycleStartWorldTime = -1.0;
	}
	UpdatePerformerTickEnabled();
}

void UGodfreyPerformerAnimationBridgeComponent::ScheduleSeaIdleEarlyChainAdvance()
{
	ClearSeaIdleChainTimer();
	UWorld* const World = GetWorld();
	if (!World)
	{
		return;
	}

	// Start the next AS before this one's BlendOut; slot source is IdleStanding (not RefPose).
	const float WallSeconds = FMath::Max(0.25f, SpeakingIdleMontageWallCycleSeconds);
	const float LeadSeconds = FMath::Clamp(GetSeaIdleMontageBlendOut(), 0.25f, WallSeconds * 0.5f);
	const float DelaySeconds = FMath::Max(0.2f, WallSeconds - LeadSeconds);

	World->GetTimerManager().SetTimer(
		SeaIdleChainTimerHandle,
		FTimerDelegate::CreateUObject(this, &UGodfreyPerformerAnimationBridgeComponent::AdvanceSeaIdleChain),
		DelaySeconds,
		false);

	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("GodfreyPerformerBridge [SeaIdle]: early chain in %.2fs (wall=%.2fs lead=%.2fs) — overlap avoids RefPose."),
		DelaySeconds, WallSeconds, LeadSeconds);
}

void UGodfreyPerformerAnimationBridgeComponent::AdvanceSeaIdleChain()
{
	ClearSeaIdleChainTimer();

	if (bOperatorPerformanceHold)
	{
		return;
	}

	if (PerformerState)
	{
		if (PerformerState->IsInDialog()
			|| PerformerState->GetExhibitionPresence() != EGodfreyExhibitionPresence::SeaIdle)
		{
			return;
		}
	}
	if (bIsSpeaking)
	{
		return;
	}

	if (bDriveExhibitionPresenceMontages || bEnableBodyMontagesWhenLibraryReady)
	{
		bEnableBodyMontages = true;
	}

	StopIdleBreathingMontageIfActive();
	EnsureMontageAnimInstanceReady();
	EnsureBodyMontagePlaybackReady();

	UAnimMontage* Montage = PickSeaIdleMontageFromPool(TEXT("SeaIdle"));
	if (!Montage)
	{
		const TSoftObjectPtr<UAnimSequence> CalmSeaIdleSequence =
			DefaultSeaIdleSequence.ToSoftObjectPath().IsValid()
				? DefaultSeaIdleSequence
				: TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(
					TEXT("/Game/Godfrey/Animation/Animation/Performances/AS_IdleLookingToSea_01.AS_IdleLookingToSea_01")));
		ClearMontageSlotIfStemEquals(SeaIdleMontage, TEXT("IdleStanding_01"));
		Montage = ResolvePresenceMontageSlot(SeaIdleMontage, CalmSeaIdleSequence, TEXT("SeaIdle"), 1);
	}
	if (!Montage)
	{
		UE_LOG(LogGodfreyPerformance, Warning,
			TEXT("GodfreyPerformerBridge: SeaIdle advance failed — pool/fallback empty."));
		return;
	}

	const bool bPlayed = PlayMontageIfPossible(
		Montage,
		TEXT("SeaIdle"),
		SeaIdleMontagePlayRate,
		/*bRestartIfAlreadyPlaying=*/true,
		/*bLoopMontage=*/false,
		/*bChainAsHold=*/true,
		/*bSoftSlotReplace=*/true);
	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("GodfreyPerformerBridge: sea idle advance '%s' played=%d (soft crossfade)."),
		*Montage->GetName(), bPlayed ? 1 : 0);
	if (bPlayed)
	{
		ScheduleSeaIdleEarlyChainAdvance();
	}
}

void UGodfreyPerformerAnimationBridgeComponent::PlaySeaIdleLoop()
{
	if (bOperatorPerformanceHold)
	{
		return;
	}

	if (PerformerState && PerformerState->IsInDialog())
	{
		ClearSeaIdleChainTimer();
		UE_LOG(LogGodfreyPerformance, Log,
			TEXT("GodfreyPerformerBridge: skipping SeaIdle — in dialog (R7); using attentive listening hold."));
		PlayAwaitingConversationHoldMontage(TEXT("DialogIdle"), true);
		return;
	}

	if (bDriveExhibitionPresenceMontages || bEnableBodyMontagesWhenLibraryReady)
	{
		bEnableBodyMontages = true;
	}

	// EnsurePresenceIdle retries must not interrupt an active soft-chained one-shot.
	if (IsSeaIdleChainActive())
	{
		UE_LOG(LogGodfreyPerformance, Verbose,
			TEXT("GodfreyPerformerBridge: SeaIdle chain already active — skip restart."));
		return;
	}

	AdvanceSeaIdleChain();
}

void UGodfreyPerformerAnimationBridgeComponent::ScheduleEnsurePresenceIdleAtBeginPlay()
{
	PresenceIdleEnsureAttempts = 0;
	UWorld* const World = GetWorld();
	if (!World)
	{
		EnsurePresenceIdlePlaying();
		return;
	}

	World->GetTimerManager().ClearTimer(PresenceIdleRetryTimerHandle);
	World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateUObject(this,
		&UGodfreyPerformerAnimationBridgeComponent::EnsurePresenceIdlePlaying));
	// MetaHuman Body AnimInstance often appears a few frames after BeginPlay.
	World->GetTimerManager().SetTimer(PresenceIdleRetryTimerHandle,
		FTimerDelegate::CreateUObject(this, &UGodfreyPerformerAnimationBridgeComponent::EnsurePresenceIdlePlaying),
		0.35f, false);
}

void UGodfreyPerformerAnimationBridgeComponent::EnsurePresenceIdlePlaying()
{
	if (bOperatorPerformanceHold)
	{
		return;
	}

	++PresenceIdleEnsureAttempts;

	if (PerformerState && PerformerState->GetExhibitionPresence() != EGodfreyExhibitionPresence::SeaIdle
		&& PerformerState->HasEngagedVisitor())
	{
		return;
	}
	if (bIsSpeaking)
	{
		return;
	}

	if (bDriveExhibitionPresenceMontages)
	{
		PlaySeaIdleLoop();
	}
	else
	{
		TryStartIdleBreathingMontage();
	}

	const bool bAnyPlaying = IsBodyMontagePlaying();
	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("GodfreyPerformerBridge: EnsurePresenceIdlePlaying attempt=%d anyMontage=%d presence=%d."),
		PresenceIdleEnsureAttempts, bAnyPlaying ? 1 : 0,
		PerformerState ? static_cast<int32>(PerformerState->GetExhibitionPresence()) : -1);

	if (!bAnyPlaying && PresenceIdleEnsureAttempts < 6)
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(PresenceIdleRetryTimerHandle,
				FTimerDelegate::CreateUObject(this, &UGodfreyPerformerAnimationBridgeComponent::EnsurePresenceIdlePlaying),
				0.4f, false);
		}
	}
}

void UGodfreyPerformerAnimationBridgeComponent::PlayEngageSequence()
{
	if (bSkipEngageTurnMontage)
	{
		UE_LOG(LogGodfreyPerformance, Log,
			TEXT("GodfreyPerformerBridge: skipping EngageTurn (bSkipEngageTurnMontage=1) — camera already faces sea-idle."));
		AdvanceEngageAfterTurn();
		return;
	}
	UAnimMontage* const Turn = ResolvePresenceMontageSlot(EngageTurnMontage, DefaultEngageTurnSequence, TEXT("EngageTurn"), 1);
	PlayPresenceMontageChainStep(Turn, TEXT("EngageTurn"), EngageChainTimerHandle,
		FTimerDelegate::CreateUObject(this, &UGodfreyPerformerAnimationBridgeComponent::AdvanceEngageAfterTurn),
		2.8f);
}

void UGodfreyPerformerAnimationBridgeComponent::ArmPresenceWelcomeEngage()
{
	bForceNextEngageGreetMontage = true;
	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("GodfreyPerformerBridge: presence Welcome armed for next engage (R17)."));
}

void UGodfreyPerformerAnimationBridgeComponent::AdvanceEngageAfterTurn()
{
	const bool bSkipGreet = bSkipEngageGreetMontage && !bForceNextEngageGreetMontage;
	bForceNextEngageGreetMontage = false;
	if (bSkipGreet)
	{
		UE_LOG(LogGodfreyPerformance, Log,
			TEXT("GodfreyPerformerBridge: skipping EngageGreet (bSkipEngageGreetMontage=1) — Welcome reserved for presence (R2/R17)."));
		AdvanceEngageAfterGreet();
		return;
	}
	UAnimMontage* const Greet = ResolvePresenceMontageSlot(EngageGreetMontage, DefaultEngageGreetSequence, TEXT("EngageGreet"), 1);
	PlayPresenceMontageChainStep(Greet, TEXT("EngageGreet"), EngageChainTimerHandle,
		FTimerDelegate::CreateUObject(this, &UGodfreyPerformerAnimationBridgeComponent::AdvanceEngageAfterGreet),
		2.5f);
}

void UGodfreyPerformerAnimationBridgeComponent::AdvanceEngageAfterGreet()
{
	if (PerformerState)
	{
		PerformerState->NotifyEngageSequenceFinished();
	}
}

void UGodfreyPerformerAnimationBridgeComponent::PlayFarewellSequence()
{
	StopSpeakingBehaviour();
	UAnimMontage* const Wave = ResolvePresenceMontageSlot(FarewellWaveMontage, DefaultFarewellWaveSequence, TEXT("FarewellWave"), 1);
	PlayPresenceMontageChainStep(Wave, TEXT("FarewellWave"), FarewellChainTimerHandle,
		FTimerDelegate::CreateUObject(this, &UGodfreyPerformerAnimationBridgeComponent::AdvanceFarewellAfterWave),
		2.8f);
}

void UGodfreyPerformerAnimationBridgeComponent::AdvanceFarewellAfterWave()
{
	UAnimMontage* const Back = ResolvePresenceMontageSlot(BackToSeaMontage, DefaultBackToSeaSequence, TEXT("BackToSea"), 1);
	PlayPresenceMontageChainStep(Back, TEXT("BackToSea"), FarewellChainTimerHandle,
		FTimerDelegate::CreateUObject(this, &UGodfreyPerformerAnimationBridgeComponent::AdvanceFarewellAfterBackToSea),
		3.0f);
}

void UGodfreyPerformerAnimationBridgeComponent::AdvanceFarewellAfterBackToSea()
{
	if (PerformerState)
	{
		PerformerState->NotifyFarewellSequenceFinished();
	}
	else
	{
		PlaySeaIdleLoop();
	}
}

FKey UGodfreyPerformerAnimationBridgeComponent::ResolveOperatorCaptureKey() const
{
	return DebugPerformancePlayKey.IsValid() ? DebugPerformancePlayKey : EKeys::K;
}

void UGodfreyPerformerAnimationBridgeComponent::TryRecoverStuckEngageChain(float DeltaTime)
{
	if (!PerformerState || PerformerState->GetExhibitionPresence() != EGodfreyExhibitionPresence::Engaging)
	{
		StuckEngageEmptySeconds = 0.f;
		return;
	}

	UWorld* const World = GetWorld();
	const bool bChainPending = World && World->GetTimerManager().IsTimerActive(EngageChainTimerHandle);
	if (bChainPending || IsBodyMontagePlaying())
	{
		StuckEngageEmptySeconds = 0.f;
		return;
	}

	StuckEngageEmptySeconds += DeltaTime;
	if (StuckEngageEmptySeconds < 0.45f)
	{
		return;
	}

	StuckEngageEmptySeconds = 0.f;
	UE_LOG(LogGodfreyPerformance, Warning,
		TEXT("GodfreyPerformerBridge: engage chain stalled (no montage, timer inactive) — recovering to listening."));
	AdvanceEngageAfterGreet();
}

void UGodfreyPerformerAnimationBridgeComponent::PollOperatorCaptureKey()
{
#if UE_BUILD_SHIPPING
	return;
#else
	if (!bEnableDebugPerformancePlayKey)
	{
		return;
	}
	const FKey CaptureKey = ResolveOperatorCaptureKey();
	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (!PC)
	{
		PC = UGameplayStatics::GetPlayerController(GetWorld(), 0);
	}
	if (!PC || !PC->WasInputKeyJustPressed(CaptureKey))
	{
		return;
	}

	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("GodfreyPerformerBridge: operator key '%s' pressed — capture cue='%s'."),
		*CaptureKey.ToString(), *DebugPerformancePlayCueId);
	PlayOperatorPerformanceClip(DebugPerformancePlayCueId);
#endif
}

void UGodfreyPerformerAnimationBridgeComponent::CancelOperatorCaptureTimers()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(OperatorCaptureWarmupTimerHandle);
		World->GetTimerManager().ClearTimer(OperatorCaptureFinishTimerHandle);
	}
}

void UGodfreyPerformerAnimationBridgeComponent::EnsureBodyAnimBlueprintMode(const TCHAR* Reason)
{
	if (!IsValid(TargetSkeletalMesh))
	{
		return;
	}
	if (TargetSkeletalMesh->GetAnimationMode() == EAnimationMode::AnimationBlueprint)
	{
		if (UAnimInstance* AnimInst = TargetSkeletalMesh->GetAnimInstance())
		{
			if (!AnimInst->IsA<UAnimSingleNodeInstance>())
			{
				return;
			}
		}
		else
		{
			return;
		}
	}

	const TSubclassOf<UAnimInstance> ExistingAnimClass = TargetSkeletalMesh->GetAnimClass();
	if (!ExistingAnimClass || ExistingAnimClass == UAnimSingleNodeInstance::StaticClass())
	{
		return;
	}

	TargetSkeletalMesh->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	TargetSkeletalMesh->SetAnimInstanceClass(ExistingAnimClass);
	TargetSkeletalMesh->InitAnim(true);
	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("GodfreyPerformerBridge: restored Body AnimationBlueprint from SingleNode (%s)."),
		Reason ? Reason : TEXT("(none)"));
}

void UGodfreyPerformerAnimationBridgeComponent::StartOperatorCapture(const FString& CueId)
{
	PendingOperatorCaptureCueId = CueId;
	bOperatorPerformanceHold = true;
	CancelOperatorCaptureTimers();
	ClearSeaIdleChainTimer();
	ClearSpeakingIdleChainTimer();
	ClearDialogIdleChainTimer();
	EnsureBodyAnimBlueprintMode(TEXT("OperatorCaptureStart"));
	SuspendInteractiveExhibit();
	UpdatePerformerTickEnabled();

	const bool bRecorderArmed = bCaptureTakeRecorderOnDebugPlay && BeginTakeRecorderIfPossible();
	float WarmupSeconds = 0.05f;
	if (bRecorderArmed)
	{
		WarmupSeconds = FMath::Max(0.05f, OperatorCaptureRecorderWarmupSeconds);
#if WITH_EDITOR
		const FTakeRecorderParameters Params = UTakeRecorderBlueprintLibrary::GetDefaultParameters();
		WarmupSeconds = FMath::Max(WarmupSeconds, Params.User.CountdownSeconds + 0.2f);
#endif
	}

	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("GodfreyPerformerBridge: operator capture start cue='%s' takeRecorder=%d warmup=%.2fs (interactive exhibit suspended; world NOT paused)."),
		*CueId, bRecorderArmed ? 1 : 0, WarmupSeconds);

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			OperatorCaptureWarmupTimerHandle,
			FTimerDelegate::CreateUObject(this, &UGodfreyPerformerAnimationBridgeComponent::PlayOperatorClipAfterWarmup),
			WarmupSeconds,
			false);
	}
	else
	{
		PlayOperatorClipAfterWarmup();
	}
}

void UGodfreyPerformerAnimationBridgeComponent::PlayOperatorClipAfterWarmup()
{
	if (!bOperatorPerformanceHold)
	{
		return;
	}

	EnsureBodyAnimBlueprintMode(TEXT("OperatorCapturePlay"));
	const bool bPlayed = PlayNamedPerformanceAction(PendingOperatorCaptureCueId, true, true);
	UAnimSequence* Sequence = LoadLibrarySequenceByStem(PendingOperatorCaptureCueId);
	if (!Sequence)
	{
		bool bInterrupt = true;
		if (UAnimMontage* Montage = ResolveNamedActionMontage(PendingOperatorCaptureCueId, bInterrupt))
		{
			Sequence = ExtractPrimarySequenceFromMontage(Montage);
		}
	}
	if (!bPlayed)
	{
		UE_LOG(LogGodfreyPerformance, Warning,
			TEXT("GodfreyPerformerBridge: operator clip '%s' failed to play — aborting capture."),
			*PendingOperatorCaptureCueId);
		FinishOperatorCapture();
		return;
	}
	BeginFaceCurveOverlay(Sequence);

	float ClipSeconds = Sequence ? Sequence->GetPlayLength() : 4.f;
	if (ClipSeconds < 0.25f)
	{
		ClipSeconds = 4.f;
	}
	const float FinishSeconds = ClipSeconds + FMath::Max(0.f, OperatorCaptureRecorderTailSeconds);

	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("GodfreyPerformerBridge: operator clip '%s' played=%d seq='%s' len=%.2fs finishIn=%.2fs."),
		*PendingOperatorCaptureCueId,
		bPlayed ? 1 : 0,
		Sequence ? *Sequence->GetName() : TEXT("(none)"),
		ClipSeconds,
		FinishSeconds);

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			OperatorCaptureFinishTimerHandle,
			FTimerDelegate::CreateUObject(this, &UGodfreyPerformerAnimationBridgeComponent::FinishOperatorCapture),
			FinishSeconds,
			false);
	}
	else
	{
		FinishOperatorCapture();
	}
}

void UGodfreyPerformerAnimationBridgeComponent::FinishOperatorCapture()
{
	CancelOperatorCaptureTimers();
	StopFaceCurveOverlay();
	EndTakeRecorderIfPossible();

	bOperatorPerformanceHold = false;
	PendingOperatorCaptureCueId.Reset();
	ResumeInteractiveExhibit();
	UpdatePerformerTickEnabled();

	if (PerformerState && PerformerState->IsInDialog())
	{
		PlayAwaitingConversationHoldMontage(TEXT("OperatorCaptureResume"), true);
	}
	else
	{
		PlaySeaIdleLoop();
	}

	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("GodfreyPerformerBridge: operator capture finished — interactive exhibit resumed."));
}

void UGodfreyPerformerAnimationBridgeComponent::SuspendInteractiveExhibit()
{
	bOperatorSuspendedVoice = false;
	bOperatorSuspendedQueuePoll = false;
	bOperatorMutedEngageOnPresence = false;

	AActor* const Owner = GetOwner();
	UWorld* const World = GetWorld();
	AActor* const GameMode = World ? static_cast<AActor*>(World->GetAuthGameMode()) : nullptr;

	auto FindVoice = [&]() -> UGodfreyVoiceInputComponent*
	{
		if (Owner)
		{
			if (UGodfreyVoiceInputComponent* Voice = Owner->FindComponentByClass<UGodfreyVoiceInputComponent>())
			{
				return Voice;
			}
		}
		if (GameMode)
		{
			if (UGodfreyVoiceInputComponent* Voice = GameMode->FindComponentByClass<UGodfreyVoiceInputComponent>())
			{
				return Voice;
			}
		}
		if (World)
		{
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				if (UGodfreyVoiceInputComponent* Voice = (*It)->FindComponentByClass<UGodfreyVoiceInputComponent>())
				{
					return Voice;
				}
			}
		}
		return nullptr;
	};

	if (UGodfreyVoiceInputComponent* Voice = FindVoice())
	{
		if (Voice->IsListening())
		{
			Voice->StopListening();
			bOperatorSuspendedVoice = true;
		}
	}

	UGodfreyExhibitionQueuePollComponent* Poll = nullptr;
	if (GameMode)
	{
		Poll = GameMode->FindComponentByClass<UGodfreyExhibitionQueuePollComponent>();
	}
	if (!Poll && Owner)
	{
		Poll = Owner->FindComponentByClass<UGodfreyExhibitionQueuePollComponent>();
	}
	if (Poll)
	{
		Poll->StopPolling();
		bOperatorSuspendedQueuePoll = true;
	}

	UGodfreyVisitorPresenceComponent* Presence = nullptr;
	if (GameMode)
	{
		Presence = GameMode->FindComponentByClass<UGodfreyVisitorPresenceComponent>();
	}
	if (!Presence && Owner)
	{
		Presence = Owner->FindComponentByClass<UGodfreyVisitorPresenceComponent>();
	}
	if (Presence && Presence->bEngageOnPresence)
	{
		bOperatorSavedEngageOnPresence = Presence->bEngageOnPresence;
		Presence->bEngageOnPresence = false;
		bOperatorMutedEngageOnPresence = true;
	}

	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("GodfreyPerformerBridge: exhibit suspended (voice=%d poll=%d presenceEngage=%d)."),
		bOperatorSuspendedVoice ? 1 : 0,
		bOperatorSuspendedQueuePoll ? 1 : 0,
		bOperatorMutedEngageOnPresence ? 1 : 0);
}

void UGodfreyPerformerAnimationBridgeComponent::ResumeInteractiveExhibit()
{
	AActor* const Owner = GetOwner();
	UWorld* const World = GetWorld();
	AActor* const GameMode = World ? static_cast<AActor*>(World->GetAuthGameMode()) : nullptr;

	if (bOperatorSuspendedVoice)
	{
		UGodfreyVoiceInputComponent* Voice = nullptr;
		if (Owner)
		{
			Voice = Owner->FindComponentByClass<UGodfreyVoiceInputComponent>();
		}
		if (!Voice && GameMode)
		{
			Voice = GameMode->FindComponentByClass<UGodfreyVoiceInputComponent>();
		}
		if (Voice)
		{
			Voice->StartListening();
		}
		bOperatorSuspendedVoice = false;
	}

	if (bOperatorSuspendedQueuePoll)
	{
		UGodfreyExhibitionQueuePollComponent* Poll = nullptr;
		if (GameMode)
		{
			Poll = GameMode->FindComponentByClass<UGodfreyExhibitionQueuePollComponent>();
		}
		if (!Poll && Owner)
		{
			Poll = Owner->FindComponentByClass<UGodfreyExhibitionQueuePollComponent>();
		}
		if (Poll)
		{
			Poll->StartPolling();
		}
		bOperatorSuspendedQueuePoll = false;
	}

	if (bOperatorMutedEngageOnPresence)
	{
		UGodfreyVisitorPresenceComponent* Presence = nullptr;
		if (GameMode)
		{
			Presence = GameMode->FindComponentByClass<UGodfreyVisitorPresenceComponent>();
		}
		if (!Presence && Owner)
		{
			Presence = Owner->FindComponentByClass<UGodfreyVisitorPresenceComponent>();
		}
		if (Presence)
		{
			Presence->bEngageOnPresence = bOperatorSavedEngageOnPresence;
		}
		bOperatorMutedEngageOnPresence = false;
	}
}

void UGodfreyPerformerAnimationBridgeComponent::BeginFaceCurveOverlay(UAnimSequence* Sequence)
{
	StopFaceCurveOverlay();
	if (!IsValid(Sequence))
	{
		UE_LOG(LogGodfreyPerformance, Warning,
			TEXT("GodfreyPerformerBridge: operator face overlay skipped — no sequence."));
		return;
	}

	USkeletalMeshComponent* const Face = FindFollowerMeshByComponentName(FName(TEXT("Face")));
	if (!IsValid(Face))
	{
		UE_LOG(LogGodfreyPerformance, Warning,
			TEXT("GodfreyPerformerBridge: operator face overlay skipped — no Face mesh."));
		return;
	}

	if (const IAnimationDataModel* Model = Sequence->GetDataModel())
	{
		for (const FFloatCurve& Curve : Model->GetFloatCurves())
		{
			const FName CurveName = Curve.GetName();
			if (!CurveName.IsNone())
			{
				OperatorFaceCurveNames.Add(CurveName);
			}
		}
	}

	OperatorFaceCurveSequence = Sequence;
	OperatorFaceCurveStartWorldTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	Face->AddTickPrerequisiteComponent(this);

	if (UAnimInstance* FaceAnim = Face->GetAnimInstance())
	{
		OperatorFaceSlotMontage = FaceAnim->PlaySlotAnimationAsDynamicMontage(
			Sequence,
			UGodfreyBodyAnimInstance::DefaultBodyMontageSlotName,
			GetBodyMontageBlendIn(),
			GetBodyMontageBlendOut(),
			1.f,
			1);
		UE_LOG(LogGodfreyPerformance, Log,
			TEXT("GodfreyPerformerBridge: Face slot montage from '%s' on '%s' -> %s (curves=%d)."),
			*Sequence->GetName(),
			*FaceAnim->GetClass()->GetName(),
			OperatorFaceSlotMontage ? *OperatorFaceSlotMontage->GetName() : TEXT("(incompatible skeleton — curve overlay only)"),
			OperatorFaceCurveNames.Num());
	}
	else
	{
		UE_LOG(LogGodfreyPerformance, Warning,
			TEXT("GodfreyPerformerBridge: Face has no AnimInstance — cannot overlay facial performance."));
	}
}

void UGodfreyPerformerAnimationBridgeComponent::TickFaceCurveOverlay()
{
	if (!IsValid(OperatorFaceCurveSequence) || OperatorFaceCurveNames.Num() == 0)
	{
		return;
	}
	USkeletalMeshComponent* const Face = FindFollowerMeshByComponentName(FName(TEXT("Face")));
	UAnimInstance* const FaceAnim = IsValid(Face) ? Face->GetAnimInstance() : nullptr;
	if (!FaceAnim)
	{
		return;
	}

	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : OperatorFaceCurveStartWorldTime;
	const float ClipLen = FMath::Max(0.01f, OperatorFaceCurveSequence->GetPlayLength());
	const float Time = FMath::Clamp(static_cast<float>(Now - OperatorFaceCurveStartWorldTime), 0.f, ClipLen);
	const FAnimExtractContext Extract(static_cast<double>(Time));
	for (const FName CurveName : OperatorFaceCurveNames)
	{
		const float Value = OperatorFaceCurveSequence->EvaluateCurveData(CurveName, Extract);
		FaceAnim->OverrideCurveValue(CurveName, Value);
	}
}

void UGodfreyPerformerAnimationBridgeComponent::StopFaceCurveOverlay()
{
	if (USkeletalMeshComponent* Face = FindFollowerMeshByComponentName(FName(TEXT("Face"))))
	{
		Face->RemoveTickPrerequisiteComponent(this);
		if (OperatorFaceSlotMontage)
		{
			if (UAnimInstance* FaceAnim = Face->GetAnimInstance())
			{
				if (FaceAnim->Montage_IsActive(OperatorFaceSlotMontage))
				{
					FaceAnim->Montage_Stop(GetBodyMontageBlendOut(), OperatorFaceSlotMontage);
				}
			}
		}
	}
	OperatorFaceSlotMontage = nullptr;
	OperatorFaceCurveSequence = nullptr;
	OperatorFaceCurveNames.Reset();
	OperatorFaceCurveStartWorldTime = -1.0;
}

bool UGodfreyPerformerAnimationBridgeComponent::BeginTakeRecorderIfPossible()
{
#if WITH_EDITOR
	if (!bCaptureTakeRecorderOnDebugPlay)
	{
		return false;
	}

	UTakeRecorderPanel* Panel = UTakeRecorderBlueprintLibrary::GetTakeRecorderPanel();
	if (!Panel)
	{
		Panel = UTakeRecorderBlueprintLibrary::OpenTakeRecorderPanel();
	}
	if (!Panel)
	{
		UE_LOG(LogGodfreyPerformance, Warning,
			TEXT("GodfreyPerformerBridge: Take Recorder panel could not be opened. Open Window > Cinematics > Take Recorder, add BP_Godfrey_Performer as an Actor source, then press K again."));
		return false;
	}

	if (UTakeRecorderSources* Sources = Panel->GetSources())
	{
		bool bHasGodfrey = false;
		AActor* const Owner = GetOwner();
		for (UTakeRecorderSource* Source : Sources->GetSources())
		{
			if (const UTakeRecorderActorSource* ActorSource = Cast<UTakeRecorderActorSource>(Source))
			{
				if (ActorSource->GetSourceActor().Get() == Owner)
				{
					bHasGodfrey = true;
					break;
				}
			}
		}
		if (!bHasGodfrey && Owner)
		{
			UTakeRecorderActorSource::AddSourceForActor(Owner, Sources);
			UE_LOG(LogGodfreyPerformance, Log,
				TEXT("GodfreyPerformerBridge: added '%s' as Take Recorder Actor source."),
				*Owner->GetActorNameOrLabel());
		}
		for (UTakeRecorderSource* Source : Sources->GetSources())
		{
			if (UTakeRecorderActorSource* ActorSource = Cast<UTakeRecorderActorSource>(Source))
			{
				if (ActorSource->GetSourceActor().Get() == Owner)
				{
					ActorSource->RecordType = ETakeRecorderActorRecordType::Possessable;
				}
			}
		}
	}

	if (UTakeRecorderBlueprintLibrary::IsRecording())
	{
		UE_LOG(LogGodfreyPerformance, Log,
			TEXT("GodfreyPerformerBridge: Take Recorder already running — reusing active take."));
		bOperatorTakeRecorderStarted = false;
		return true;
	}

	FText ErrorText;
	if (!Panel->CanStartRecording(ErrorText))
	{
		UE_LOG(LogGodfreyPerformance, Warning,
			TEXT("GodfreyPerformerBridge: Take Recorder cannot start: %s"),
			*ErrorText.ToString());
		return false;
	}

	Panel->StartRecording();
	bOperatorTakeRecorderStarted = UTakeRecorderBlueprintLibrary::IsRecording()
		|| UTakeRecorderBlueprintLibrary::GetActiveRecorder() != nullptr;
	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("GodfreyPerformerBridge: Take Recorder StartRecording started=%d."),
		bOperatorTakeRecorderStarted ? 1 : 0);
	return bOperatorTakeRecorderStarted;
#else
	UE_LOG(LogGodfreyPerformance, Warning,
		TEXT("GodfreyPerformerBridge: Take Recorder capture is editor-only."));
	return false;
#endif
}

void UGodfreyPerformerAnimationBridgeComponent::EndTakeRecorderIfPossible()
{
#if WITH_EDITOR
	if (!bOperatorTakeRecorderStarted && !UTakeRecorderBlueprintLibrary::IsRecording())
	{
		bOperatorTakeRecorderStarted = false;
		return;
	}

	if (UTakeRecorderBlueprintLibrary::IsRecording() || UTakeRecorderBlueprintLibrary::GetActiveRecorder())
	{
		UTakeRecorderBlueprintLibrary::StopRecording();
	}

	UTakeRecorderPanel* const Panel = UTakeRecorderBlueprintLibrary::GetTakeRecorderPanel();
	ULevelSequence* const LastTake = Panel ? Panel->GetLastRecordedLevelSequence() : nullptr;
	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("GodfreyPerformerBridge: Take Recorder stopped. Saved take='%s'."),
		LastTake ? *LastTake->GetPathName() : TEXT("(pending/unknown — check Content/Cinematics/Takes)"));
	bOperatorTakeRecorderStarted = false;
#else
	bOperatorTakeRecorderStarted = false;
#endif
}

namespace
{
UGodfreyPerformerAnimationBridgeComponent* FindGodfreyAnimationBridge(UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}

	UGodfreyPerformerAnimationBridgeComponent* Fallback = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* const Actor = *It;
		if (!Actor)
		{
			continue;
		}
		UGodfreyPerformerAnimationBridgeComponent* const Bridge =
			Actor->FindComponentByClass<UGodfreyPerformerAnimationBridgeComponent>();
		if (!Bridge)
		{
			continue;
		}
		const FString Label = Actor->GetActorNameOrLabel();
		if (Label.Equals(TEXT("BP_Godfrey_Performer"), ESearchCase::IgnoreCase)
			|| Actor->GetName().Contains(TEXT("BP_Godfrey_Performer")))
		{
			return Bridge;
		}
		if (!Fallback)
		{
			Fallback = Bridge;
		}
	}
	return Fallback;
}

bool IsSlateTypingWidget(const TSharedPtr<SWidget>& Widget)
{
	if (!Widget.IsValid())
	{
		return false;
	}
	const FString TypeName = Widget->GetType().ToString();
	return TypeName.Contains(TEXT("Editable"))
		|| TypeName.Contains(TEXT("MultiLine"))
		|| TypeName.Contains(TEXT("SearchBox"))
		|| TypeName.Contains(TEXT("Console"));
}

class FGodfreyOperatorCaptureInputProcessor final : public IInputProcessor
{
public:
	virtual void Tick(const float, FSlateApplication&, TSharedRef<ICursor>) override {}

	virtual const TCHAR* GetDebugName() const override { return TEXT("GodfreyOperatorCapture"); }

	virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override
	{
		if (InKeyEvent.IsRepeat())
		{
			return false;
		}

		const FKey Key = InKeyEvent.GetKey();
		if (Key != EKeys::K && Key != EKeys::F7)
		{
			return false;
		}

		UWorld* PlayWorld = nullptr;
		if (GEngine)
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (UWorld* Candidate = Context.World())
				{
					if (Candidate->WorldType == EWorldType::PIE || Candidate->WorldType == EWorldType::Game)
					{
						PlayWorld = Candidate;
						break;
					}
				}
			}
		}
		if (!PlayWorld)
		{
			return false;
		}

		const TSharedPtr<SWidget> Focused = SlateApp.GetKeyboardFocusedWidget();
		const FString FocusName = Focused.IsValid() ? Focused->GetType().ToString() : TEXT("(none)");
		if (IsSlateTypingWidget(Focused))
		{
			UE_LOG(LogGodfreyPerformance, Log,
				TEXT("GodfreyPerformerBridge: operator Slate key '%s' ignored (typing in %s)."),
				*Key.ToString(), *FocusName);
			return false;
		}

		UGodfreyPerformerAnimationBridgeComponent* const Bridge = FindGodfreyAnimationBridge(PlayWorld);
		if (!Bridge || !Bridge->bEnableDebugPerformancePlayKey)
		{
			return false;
		}

		const FKey Wanted = Bridge->DebugPerformancePlayKey.IsValid() ? Bridge->DebugPerformancePlayKey : EKeys::K;
		if (Key != Wanted && Key != EKeys::F7)
		{
			return false;
		}

		UE_LOG(LogGodfreyPerformance, Log,
			TEXT("GodfreyPerformerBridge: operator Slate key '%s' (focus=%s) — capture cue='%s'."),
			*Key.ToString(), *FocusName, *Bridge->DebugPerformancePlayCueId);
		Bridge->PlayOperatorPerformanceClip(Bridge->DebugPerformancePlayCueId);
		return true;
	}
};

static TSharedPtr<IInputProcessor> GGodfreyOperatorCaptureInputProcessor;
static int32 GGodfreyOperatorCaptureInputProcessorUsers = 0;

void GodfreyPlayActionCommand(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
{
	if (Args.Num() < 1 || Args[0].IsEmpty())
	{
		Ar.Log(TEXT("godfrey.PlayAction <CueId>  e.g. godfrey.PlayAction MHP_DuckUnderBanner_01"));
		return;
	}

	UWorld* PlayWorld = World;
	if (!PlayWorld && GEngine)
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (UWorld* Candidate = Context.World())
			{
				if (Candidate->WorldType == EWorldType::PIE || Candidate->WorldType == EWorldType::Game)
				{
					PlayWorld = Candidate;
					break;
				}
			}
		}
	}

	if (!PlayWorld)
	{
		Ar.Log(TEXT("godfrey.PlayAction: no PIE/game world — start PIE first."));
		return;
	}

	UGodfreyPerformerAnimationBridgeComponent* const Bridge = FindGodfreyAnimationBridge(PlayWorld);
	if (!Bridge)
	{
		Ar.Log(TEXT("godfrey.PlayAction: no GodfreyPerformerAnimationBridge on a level actor."));
		return;
	}

	const FString CueId = Args[0];
	const bool bPlayed = Bridge->PlayNamedPerformanceAction(CueId, true, true);
	Ar.Logf(TEXT("godfrey.PlayAction '%s' played=%d"), *CueId, bPlayed ? 1 : 0);
}

void GodfreyOperatorCaptureCommand(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
{
	UWorld* PlayWorld = World;
	if (!PlayWorld && GEngine)
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (UWorld* Candidate = Context.World())
			{
				if (Candidate->WorldType == EWorldType::PIE || Candidate->WorldType == EWorldType::Game)
				{
					PlayWorld = Candidate;
					break;
				}
			}
		}
	}

	if (!PlayWorld)
	{
		Ar.Log(TEXT("godfrey.OperatorCapture: no PIE/game world — start PIE first."));
		return;
	}

	UGodfreyPerformerAnimationBridgeComponent* const Bridge = FindGodfreyAnimationBridge(PlayWorld);
	if (!Bridge)
	{
		Ar.Log(TEXT("godfrey.OperatorCapture: no GodfreyPerformerAnimationBridge on a level actor."));
		return;
	}

	const FString CueId = Args.Num() > 0 ? Args[0] : Bridge->DebugPerformancePlayCueId;
	const bool bStarted = Bridge->PlayOperatorPerformanceClip(CueId);
	Ar.Logf(TEXT("godfrey.OperatorCapture '%s' started=%d"), *CueId, bStarted ? 1 : 0);
}

static FAutoConsoleCommandWithWorldArgsAndOutputDevice GGodfreyPlayActionCommand(
	TEXT("godfrey.PlayAction"),
	TEXT("Play a named Godfrey performance AS/AM (operator / advertising). Works in SeaIdle. Example: godfrey.PlayAction MHP_DuckUnderBanner_01"),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&GodfreyPlayActionCommand));

static FAutoConsoleCommandWithWorldArgsAndOutputDevice GGodfreyOperatorCaptureCommand(
	TEXT("godfrey.OperatorCapture"),
	TEXT("Suspend exhibit, Take Recorder the clip, then resume. Example: godfrey.OperatorCapture MHP_DuckUnderBanner_01"),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&GodfreyOperatorCaptureCommand));
}

void RegisterGodfreyOperatorCaptureSlateInput()
{
	if (!FSlateApplication::IsInitialized())
	{
		UE_LOG(LogGodfreyPerformance, Warning,
			TEXT("GodfreyPerformerBridge: cannot register operator Slate input (Slate not initialized)."));
		return;
	}
	if (GGodfreyOperatorCaptureInputProcessorUsers == 0)
	{
		GGodfreyOperatorCaptureInputProcessor = MakeShared<FGodfreyOperatorCaptureInputProcessor>();
		FSlateApplication::Get().RegisterInputPreProcessor(GGodfreyOperatorCaptureInputProcessor, 0);
		UE_LOG(LogGodfreyPerformance, Log,
			TEXT("GodfreyPerformerBridge: operator Slate input processor registered (K / F7)."));
	}
	++GGodfreyOperatorCaptureInputProcessorUsers;
}

void UnregisterGodfreyOperatorCaptureSlateInput()
{
	if (GGodfreyOperatorCaptureInputProcessorUsers <= 0)
	{
		return;
	}
	--GGodfreyOperatorCaptureInputProcessorUsers;
	if (GGodfreyOperatorCaptureInputProcessorUsers == 0 && GGodfreyOperatorCaptureInputProcessor.IsValid())
	{
		if (FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().UnregisterInputPreProcessor(GGodfreyOperatorCaptureInputProcessor);
		}
		GGodfreyOperatorCaptureInputProcessor.Reset();
		UE_LOG(LogGodfreyPerformance, Log,
			TEXT("GodfreyPerformerBridge: operator Slate input processor unregistered."));
	}
}
