#include "UnrealPerformer.h"

#include "ACEBlueprintLibrary.h"
#include "Misc/App.h"
#include "Modules/ModuleManager.h"
#include "UnrealPerformerGodfreySettings.h"
#if WITH_EDITOR
#include "Settings/LevelEditorMiscSettings.h"
#endif

DEFINE_LOG_CATEGORY(LogUnrealPerformerModule);

void FUnrealPerformerGameModule::StartupModule()
{
	FDefaultGameModuleImpl::StartupModule();

	// PIE often loses editor focus (Output Log / browser). Two independent mute paths:
	// 1) Windows focus pump: AppMult = UnfocusedVolumeMultiplier when unfocused
	// 2) EditorEngine: AppMult = 0 when unfocused UNLESS bAllowBackgroundAudio
	// DefaultEngine/DefaultEditor.ini are not always enough (Saved overrides / load order).
	FApp::SetUnfocusedVolumeMultiplier(1.f);
	FApp::SetVolumeMultiplier(1.f);
#if WITH_EDITOR
	if (ULevelEditorMiscSettings* Misc = GetMutableDefault<ULevelEditorMiscSettings>())
	{
		Misc->bAllowBackgroundAudio = true;
		Misc->EditorVolumeLevel = 1.f;
		UE_LOG(LogUnrealPerformerModule, Log,
			TEXT("Godfrey audio: bAllowBackgroundAudio forced true; EditorVolumeLevel=%.3f."),
			Misc->EditorVolumeLevel);
	}
#endif
	UE_LOG(LogUnrealPerformerModule, Log,
		TEXT("Godfrey audio: UnfocusedVolumeMultiplier forced to %.3f (Get=%.3f); AppMult=%.3f."),
		1.f,
		FApp::GetUnfocusedVolumeMultiplier(),
		FApp::GetVolumeMultiplier());

	if (GetDefault<UUnrealPerformerGodfreySettings>()->bApplyAceBurstInferenceOverrideAtStartup)
	{
		UACEBlueprintLibrary::OverrideA2F3DInferenceMode(true);
		UE_LOG(LogUnrealPerformerModule, Log,
			TEXT("ACE burst mode enabled at startup (Project Settings)."));
	}

	const UUnrealPerformerGodfreySettings* GodfreySettings = GetDefault<UUnrealPerformerGodfreySettings>();
	if (GodfreySettings->bAllocateAceProviderResourcesAtGameStartup)
	{
		UACEBlueprintLibrary::AllocateA2F3DResources(GodfreySettings->GodfreyAceProviderNameForStartupAllocation);
		UE_LOG(LogUnrealPerformerModule, Log,
			TEXT("ACE warmup: AllocateA2F3DResources(%s) at game startup."),
			*GodfreySettings->GodfreyAceProviderNameForStartupAllocation.ToString());
	}
}

IMPLEMENT_PRIMARY_GAME_MODULE(FUnrealPerformerGameModule, UnrealPerformer, "UnrealPerformer");
