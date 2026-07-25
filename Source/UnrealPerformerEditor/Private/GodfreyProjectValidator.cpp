#include "GodfreyProjectValidator.h"

#include "ACEAudioCurveSourceComponent.h"
#include "Animation/AnimMontage.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/SkeletalMeshComponent.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/WorldSettings.h"
#include "GodfreyExhibitionQueuePollComponent.h"
#include "GodfreyPerformerAnimationBridgeComponent.h"
#include "GodfreyPerformanceStateComponent.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/SoftObjectPath.h"

DEFINE_LOG_CATEGORY_STATIC(LogGodfreyValidation, Log, All);

namespace GodfreyValidationPrivate
{
bool ActorOrBlueprintHasComponent(UObject* ObjectOrClass, TSubclassOf<UActorComponent> ComponentClass)
{
	if (!ObjectOrClass || !*ComponentClass)
	{
		return false;
	}

	if (const AActor* Actor = Cast<AActor>(ObjectOrClass))
	{
		return Actor->FindComponentByClass(ComponentClass) != nullptr;
	}

	UBlueprint* Blueprint = Cast<UBlueprint>(ObjectOrClass);
	if (!Blueprint)
	{
		if (const UClass* AsClass = Cast<UClass>(ObjectOrClass))
		{
			Blueprint = Cast<UBlueprint>(AsClass->ClassGeneratedBy);
			if (!Blueprint)
			{
				if (const AActor* CDO = Cast<AActor>(AsClass->GetDefaultObject()))
				{
					return CDO->FindComponentByClass(ComponentClass) != nullptr;
				}
			}
		}
	}

	if (!Blueprint)
	{
		return false;
	}

	if (Blueprint->SimpleConstructionScript)
	{
		for (const USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
		{
			if (Node && Node->ComponentClass && Node->ComponentClass->IsChildOf(ComponentClass))
			{
				return true;
			}
		}
	}

	if (UClass* Generated = Blueprint->GeneratedClass)
	{
		if (const AActor* CDO = Cast<AActor>(Generated->GetDefaultObject()))
		{
			return CDO->FindComponentByClass(ComponentClass) != nullptr;
		}
	}

	return false;
}

UBlueprint* LoadBlueprintAsset(const FString& ObjectPath)
{
	return Cast<UBlueprint>(FSoftObjectPath(ObjectPath).TryLoad());
}
} // namespace

void UGodfreyProjectValidator::AddItem(FGodfreyValidationReport& Report, const FString& CheckId, EGodfreyValidationSeverity Severity, const FString& Message)
{
	FGodfreyValidationItem Item;
	Item.CheckId = CheckId;
	Item.Severity = Severity;
	Item.Message = Message;
	Report.Items.Add(Item);

	switch (Severity)
	{
	case EGodfreyValidationSeverity::Pass:
		++Report.PassCount;
		break;
	case EGodfreyValidationSeverity::Warning:
		++Report.WarningCount;
		if (Report.Overall == EGodfreyValidationSeverity::Pass)
		{
			Report.Overall = EGodfreyValidationSeverity::Warning;
		}
		break;
	case EGodfreyValidationSeverity::Fail:
		++Report.FailCount;
		Report.Overall = EGodfreyValidationSeverity::Fail;
		break;
	}
}

FGodfreyValidationReport UGodfreyProjectValidator::RunValidation()
{
	FGodfreyValidationReport Report;

	auto CheckPlugin = [&Report](const FString& PluginName, bool bRequired)
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(PluginName);
		const bool bEnabled = Plugin.IsValid() && Plugin->IsEnabled();
		if (bEnabled)
		{
			AddItem(Report, FString::Printf(TEXT("Plugin.%s"), *PluginName), EGodfreyValidationSeverity::Pass,
				FString::Printf(TEXT("Plugin '%s' is enabled."), *PluginName));
		}
		else if (bRequired)
		{
			AddItem(Report, FString::Printf(TEXT("Plugin.%s"), *PluginName), EGodfreyValidationSeverity::Fail,
				FString::Printf(TEXT("Required plugin '%s' is missing or disabled."), *PluginName));
		}
		else
		{
			AddItem(Report, FString::Printf(TEXT("Plugin.%s"), *PluginName), EGodfreyValidationSeverity::Warning,
				FString::Printf(TEXT("Optional plugin '%s' is not enabled."), *PluginName));
		}
	};

	CheckPlugin(TEXT("NV_ACE_Reference"), true);
	CheckPlugin(TEXT("NvAudio2FaceMark"), true);
	CheckPlugin(TEXT("MetaHuman"), true);
	CheckPlugin(TEXT("MetaHumanSDK"), true);
	CheckPlugin(TEXT("MetaHumanCharacter"), false);
	CheckPlugin(TEXT("MetaHumanLiveLink"), false);

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		AddItem(Report, TEXT("Level.World"), EGodfreyValidationSeverity::Fail, TEXT("No editor world loaded."));
		return Report;
	}

	AddItem(Report, TEXT("Level.World"), EGodfreyValidationSeverity::Pass,
		FString::Printf(TEXT("Editor world: %s"), *World->GetMapName()));

	TArray<AActor*> Performers;
	TArray<AActor*> QueueOwners;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!IsValid(Actor))
		{
			continue;
		}

		const bool bHasAce = Actor->FindComponentByClass<UACEAudioCurveSourceComponent>() != nullptr;
		const bool bHasBridge = Actor->FindComponentByClass<UGodfreyPerformerAnimationBridgeComponent>() != nullptr;
		const bool bHasPerf = Actor->FindComponentByClass<UGodfreyPerformanceStateComponent>() != nullptr;
		const bool bLabelMatch = Actor->GetActorNameOrLabel() == TEXT("BP_Godfrey_Performer")
			|| Actor->GetName().Contains(TEXT("BP_Godfrey_Performer"));
		const bool bTagMatch = Actor->ActorHasTag(FName(TEXT("GodfreyCharacter")));

		if (bLabelMatch)
		{
			Performers.Add(Actor);
		}
		else if (bHasAce && (bHasBridge || bHasPerf || bTagMatch))
		{
			Performers.Add(Actor);
		}
		if (Actor->FindComponentByClass<UGodfreyExhibitionQueuePollComponent>())
		{
			QueueOwners.Add(Actor);
		}
	}

	if (Performers.Num() == 0)
	{
		AddItem(Report, TEXT("Performer.Exists"), EGodfreyValidationSeverity::Fail,
			TEXT("No Godfrey performer actor found (ACE / bridge / performance / BP_Godfrey_Performer)."));
	}
	else if (Performers.Num() > 1)
	{
		AddItem(Report, TEXT("Performer.Duplicate"), EGodfreyValidationSeverity::Fail,
			FString::Printf(TEXT("Found %d performer actors; expected exactly one BP_Godfrey_Performer."), Performers.Num()));
		for (AActor* P : Performers)
		{
			AddItem(Report, TEXT("Performer.Candidate"), EGodfreyValidationSeverity::Fail,
				FString::Printf(TEXT("  - %s"), *P->GetActorNameOrLabel()));
		}
	}
	else
	{
		AddItem(Report, TEXT("Performer.Exists"), EGodfreyValidationSeverity::Pass,
			FString::Printf(TEXT("Primary performer: %s"), *Performers[0]->GetActorNameOrLabel()));
	}

	// Queue poll is normally on GM_Godfrey_Exhibit (GameMode), not a placed level actor.
	bool bQueueOnGameMode = false;
	FString QueueSource;
	TSubclassOf<AGameModeBase> GameModeClass = nullptr;
	if (AWorldSettings* WorldSettings = World->GetWorldSettings())
	{
		GameModeClass = WorldSettings->DefaultGameMode;
	}
	if (!GameModeClass)
	{
		GameModeClass = LoadClass<AGameModeBase>(nullptr, TEXT("/Game/Godfrey/GM_Godfrey_Exhibit.GM_Godfrey_Exhibit_C"));
	}

	if (GameModeClass)
	{
		AddItem(Report, TEXT("GameMode.Assigned"), EGodfreyValidationSeverity::Pass,
			FString::Printf(TEXT("GameMode class: %s"), *GetNameSafe(GameModeClass.Get())));

		if (GodfreyValidationPrivate::ActorOrBlueprintHasComponent(GameModeClass.Get(), UGodfreyExhibitionQueuePollComponent::StaticClass()))
		{
			bQueueOnGameMode = true;
			QueueSource = FString::Printf(TEXT("GameMode '%s'"), *GetNameSafe(GameModeClass.Get()));
		}
		else if (UBlueprint* GmBp = Cast<UBlueprint>(GameModeClass->ClassGeneratedBy))
		{
			if (GodfreyValidationPrivate::ActorOrBlueprintHasComponent(GmBp, UGodfreyExhibitionQueuePollComponent::StaticClass()))
			{
				bQueueOnGameMode = true;
				QueueSource = FString::Printf(TEXT("GameMode Blueprint '%s'"), *GmBp->GetName());
			}
		}
	}
	else if (UBlueprint* GmBp = GodfreyValidationPrivate::LoadBlueprintAsset(TEXT("/Game/Godfrey/GM_Godfrey_Exhibit.GM_Godfrey_Exhibit")))
	{
		AddItem(Report, TEXT("GameMode.Assigned"), EGodfreyValidationSeverity::Warning,
			TEXT("World has no DefaultGameMode; checking /Game/Godfrey/GM_Godfrey_Exhibit asset."));
		if (GodfreyValidationPrivate::ActorOrBlueprintHasComponent(GmBp, UGodfreyExhibitionQueuePollComponent::StaticClass()))
		{
			bQueueOnGameMode = true;
			QueueSource = TEXT("GM_Godfrey_Exhibit Blueprint asset");
		}
	}
	else
	{
		AddItem(Report, TEXT("GameMode.Assigned"), EGodfreyValidationSeverity::Warning,
			TEXT("Could not resolve GameMode (expected GM_Godfrey_Exhibit)."));
	}

	if (QueueOwners.Num() > 0)
	{
		AddItem(Report, TEXT("Queue.Component"), EGodfreyValidationSeverity::Pass,
			FString::Printf(TEXT("Speech queue poll on %d level actor(s)."), QueueOwners.Num()));
	}
	else if (bQueueOnGameMode)
	{
		AddItem(Report, TEXT("Queue.Component"), EGodfreyValidationSeverity::Pass,
			FString::Printf(TEXT("Speech queue poll present on %s (expected location)."), *QueueSource));
	}
	else
	{
		AddItem(Report, TEXT("Queue.Component"), EGodfreyValidationSeverity::Fail,
			TEXT("No UGodfreyExhibitionQueuePollComponent on level actors or GameMode (speech queue unavailable)."));
	}

	if (Performers.Num() >= 1)
	{
		AActor* Performer = Performers[0];

		if (UACEAudioCurveSourceComponent* Ace = Performer->FindComponentByClass<UACEAudioCurveSourceComponent>())
		{
			AddItem(Report, TEXT("ACE.Component"), EGodfreyValidationSeverity::Pass,
				FString::Printf(TEXT("ACEAudioCurveSourceComponent present (Volume=%.2f Buffer=%.2fs)."), Ace->Volume, Ace->BufferLengthInSeconds));
		}
		else
		{
			AddItem(Report, TEXT("ACE.Component"), EGodfreyValidationSeverity::Fail,
				TEXT("UACEAudioCurveSourceComponent missing on performer."));
		}

		if (UGodfreyPerformanceStateComponent* Perf = Performer->FindComponentByClass<UGodfreyPerformanceStateComponent>())
		{
			AddItem(Report, TEXT("Behaviour.State"), EGodfreyValidationSeverity::Pass,
				TEXT("UGodfreyPerformanceStateComponent present."));
			(void)Perf;
		}
		else
		{
			AddItem(Report, TEXT("Behaviour.State"), EGodfreyValidationSeverity::Warning,
				TEXT("UGodfreyPerformanceStateComponent missing (auto speaking state will not run)."));
		}

		if (UGodfreyPerformerAnimationBridgeComponent* Bridge = Performer->FindComponentByClass<UGodfreyPerformerAnimationBridgeComponent>())
		{
			AddItem(Report, TEXT("Animation.Bridge"), EGodfreyValidationSeverity::Pass,
				TEXT("UGodfreyPerformerAnimationBridgeComponent present."));

			if (!Bridge->bEnableBodyMontages)
			{
				AddItem(Report, TEXT("Animation.BodyMontages"), EGodfreyValidationSeverity::Warning,
					TEXT("bEnableBodyMontages=false (intentional Phase 1 park until custom gesture library)."));
			}
			else if (!Bridge->SpeakingIdleMontage)
			{
				AddItem(Report, TEXT("Animation.SpeakingIdle"), EGodfreyValidationSeverity::Warning,
					TEXT("Body montages enabled but SpeakingIdleMontage is not assigned."));
			}
			else
			{
				AddItem(Report, TEXT("Animation.SpeakingIdle"), EGodfreyValidationSeverity::Pass,
					FString::Printf(TEXT("SpeakingIdleMontage=%s"), *Bridge->SpeakingIdleMontage->GetName()));
			}
		}
		else
		{
			AddItem(Report, TEXT("Animation.Bridge"), EGodfreyValidationSeverity::Warning,
				TEXT("Animation bridge missing (body behaviour will not respond to speaking state)."));
		}

		TArray<USkeletalMeshComponent*> Meshes;
		Performer->GetComponents<USkeletalMeshComponent>(Meshes);
		USkeletalMeshComponent* Body = nullptr;
		int32 LeaderFollowers = 0;
		for (USkeletalMeshComponent* Mesh : Meshes)
		{
			if (!Mesh)
			{
				continue;
			}
			const FString Name = Mesh->GetName();
			if (Name.Contains(TEXT("Body")) && !Name.Contains(TEXT("Face")))
			{
				Body = Mesh;
			}
			if (Mesh->LeaderPoseComponent.IsValid())
			{
				++LeaderFollowers;
			}
		}

		if (Body)
		{
			AddItem(Report, TEXT("MetaHuman.Body"), EGodfreyValidationSeverity::Pass,
				FString::Printf(TEXT("Body mesh '%s' found."), *Body->GetName()));
			if (Body->GetAnimClass())
			{
				AddItem(Report, TEXT("Animation.AnimBP"), EGodfreyValidationSeverity::Pass,
					FString::Printf(TEXT("Body AnimClass=%s"), *GetNameSafe(Body->GetAnimClass())));
			}
			else
			{
				AddItem(Report, TEXT("Animation.AnimBP"), EGodfreyValidationSeverity::Fail,
					TEXT("Body skeletal mesh has no AnimClass assigned."));
			}
		}
		else
		{
			AddItem(Report, TEXT("MetaHuman.Body"), EGodfreyValidationSeverity::Fail,
				TEXT("No Body skeletal mesh found on performer."));
		}

		if (LeaderFollowers > 0)
		{
			AddItem(Report, TEXT("Animation.LeaderPose"), EGodfreyValidationSeverity::Pass,
				FString::Printf(TEXT("%d clothing/follower mesh(es) use Leader Pose."), LeaderFollowers));
		}
		else
		{
			AddItem(Report, TEXT("Animation.LeaderPose"), EGodfreyValidationSeverity::Warning,
				TEXT("No LeaderPose followers detected (Torso/Legs/Feet may not follow Body)."));
		}
	}

	const TArray<FString> RequiredAssets = {
		TEXT("/Game/Godfrey/Animation/Retargeted/As_Godfrey_Talking_Anim_Montage"),
		TEXT("/Game/Godfrey_World"),
		TEXT("/Game/Godfrey/GM_Godfrey_Exhibit"),
	};

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
	for (const FString& SoftPath : RequiredAssets)
	{
		const FSoftObjectPath Path(SoftPath);
		FAssetData Data = AssetRegistry.GetAssetByObjectPath(Path);
		if (!Data.IsValid())
		{
			// Try package path lookup
			const FString PackageName = FPackageName::ObjectPathToPackageName(SoftPath);
			TArray<FAssetData> AssetsInPackage;
			AssetRegistry.GetAssetsByPackageName(*PackageName, AssetsInPackage);
			if (AssetsInPackage.Num() > 0)
			{
				Data = AssetsInPackage[0];
			}
		}

		if (Data.IsValid())
		{
			AddItem(Report, FString::Printf(TEXT("Asset.%s"), *FPaths::GetBaseFilename(SoftPath)), EGodfreyValidationSeverity::Pass,
				FString::Printf(TEXT("Asset exists: %s"), *SoftPath));
		}
		else
		{
			AddItem(Report, FString::Printf(TEXT("Asset.%s"), *FPaths::GetBaseFilename(SoftPath)), EGodfreyValidationSeverity::Fail,
				FString::Printf(TEXT("Missing asset: %s"), *SoftPath));
		}
	}

	return Report;
}

void UGodfreyProjectValidator::RunValidationAndLog()
{
	const FGodfreyValidationReport Report = RunValidation();
	const TCHAR* Overall =
		Report.Overall == EGodfreyValidationSeverity::Fail ? TEXT("FAIL")
		: (Report.Overall == EGodfreyValidationSeverity::Warning ? TEXT("WARNING") : TEXT("PASS"));

	UE_LOG(LogGodfreyValidation, Display, TEXT("========== Validate Godfrey Project: %s =========="), Overall);
	UE_LOG(LogGodfreyValidation, Display, TEXT("PASS=%d  WARNING=%d  FAIL=%d"), Report.PassCount, Report.WarningCount, Report.FailCount);

	for (const FGodfreyValidationItem& Item : Report.Items)
	{
		const TCHAR* Sev =
			Item.Severity == EGodfreyValidationSeverity::Fail ? TEXT("FAIL")
			: (Item.Severity == EGodfreyValidationSeverity::Warning ? TEXT("WARNING") : TEXT("PASS"));
		UE_LOG(LogGodfreyValidation, Display, TEXT("[%s] %s — %s"), Sev, *Item.CheckId, *Item.Message);
	}

	UE_LOG(LogGodfreyValidation, Display, TEXT("=================================================="));
}
