#include "GodfreyExhibitionQueuePollComponent.h"

#include "AsyncActionStreamGodfreySpeech.h"
#include "ACEAudioCurveSourceComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GodfreyBrainLauncher.h"
#include "GodfreyPcmStreamSession.h"
#include "GodfreyDiagnostics.h"
#include "GodfreyDirectSpeechComponent.h"
#include "GodfreyRuntimePerfHudComponent.h"
#include "GodfreyListenCueComponent.h"
#include "GodfreyVisitorPresenceComponent.h"
#include "GodfreyVisitorBriefingComponent.h"
#include "GodfreyVoiceInputComponent.h"
#include "GodfreyStageBackdropVideoComponent.h"
#include "UnrealPerformerGodfreySettings.h"
#include "AudioMixerBlueprintLibrary.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogGodfreyExhibitionQueue, Log, All);

UGodfreyExhibitionQueuePollComponent::UGodfreyExhibitionQueuePollComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	PollIntervalSeconds = GetDefault<UUnrealPerformerGodfreySettings>()->GodfreyDefaultQueuePollIntervalSeconds;
	StreamSampleRate = GetDefault<UUnrealPerformerGodfreySettings>()->GodfreyDefaultStreamSampleRate;
}

void UGodfreyExhibitionQueuePollComponent::BeginPlay()
{
	Super::BeginPlay();

	const UUnrealPerformerGodfreySettings* const Settings = GetDefault<UUnrealPerformerGodfreySettings>();
	if (Settings && Settings->bAutoStartGodfreyBrainOnBeginPlay)
	{
		const bool bOk = FGodfreyBrainLauncher::EnsureRunning(
			GodfreyBrainBaseUrl,
			Settings->GodfreyBrainWorkingDirectory,
			Settings->GodfreyBrainNodeExecutable,
			Settings->GodfreyBrainStartScript.IsEmpty() ? TEXT("server.js") : Settings->GodfreyBrainStartScript,
			Settings->bGodfreyBrainShowConsoleWindow);
		if (!bOk)
		{
			UE_LOG(LogGodfreyExhibitionQueue, Error,
				TEXT("Auto-start Godfrey Brain failed — speech/audio will stay silent until Brain is running at %s"),
				*GodfreyBrainBaseUrl);
		}
	}

#if !UE_BUILD_SHIPPING
	{
		const UUnrealPerformerGodfreySettings* HudSettings = GetDefault<UUnrealPerformerGodfreySettings>();
		if (HudSettings->bGodfreyShowRuntimePerfHud || HudSettings->bGodfreyShowCurrentAnimHud)
		{
			AActor* HudOwner = ResolveCharacterForAce();
			if (!HudOwner)
			{
				HudOwner = GetOwner();
			}
			if (HudOwner && !HudOwner->FindComponentByClass<UGodfreyRuntimePerfHudComponent>())
			{
				UGodfreyRuntimePerfHudComponent* Hud =
					NewObject<UGodfreyRuntimePerfHudComponent>(HudOwner, TEXT("GodfreyRuntimePerfHud"));
				Hud->RegisterComponent();
			}
		}
	}
#endif
	if (GetDefault<UUnrealPerformerGodfreySettings>()->bGodfreyEnableVisitorPresenceWebcam)
	{
		if (AActor* Owner = GetOwner())
		{
			if (!Owner->FindComponentByClass<UGodfreyVisitorPresenceComponent>())
			{
				UGodfreyVisitorPresenceComponent* Presence =
					NewObject<UGodfreyVisitorPresenceComponent>(Owner, TEXT("GodfreyVisitorPresence"));
				Presence->RegisterComponent();
				UE_LOG(LogGodfreyExhibitionQueue, Log, TEXT("Spawned GodfreyVisitorPresenceComponent (webcam)."));
			}
			if (!Owner->FindComponentByClass<UGodfreyVisitorBriefingComponent>())
			{
				if (!Settings || Settings->bGodfreyEnableVisitorBriefing)
				{
					UGodfreyVisitorBriefingComponent* Briefing =
						NewObject<UGodfreyVisitorBriefingComponent>(Owner, TEXT("GodfreyVisitorBriefing"));
					Briefing->RegisterComponent();
					UE_LOG(LogGodfreyExhibitionQueue, Log, TEXT("Spawned GodfreyVisitorBriefingComponent (arrival card)."));
				}
			}
		}
	}
	if (GetDefault<UUnrealPerformerGodfreySettings>()->bGodfreyHideExhibitFloorInPlay)
	{
		if (UWorld* World = GetWorld())
		{
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				AActor* const Actor = *It;
				if (Actor && Actor->GetActorNameOrLabel().Equals(TEXT("Exhibit_Floor"), ESearchCase::IgnoreCase))
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
					UE_LOG(LogGodfreyExhibitionQueue, Log, TEXT("Exhibit_Floor see-through (collision kept)."));
				}
			}
		}
	}
	if (GetDefault<UUnrealPerformerGodfreySettings>()->bGodfreyEnableStageBackdropVideo)
	{
		if (AActor* Owner = GetOwner())
		{
			if (!Owner->FindComponentByClass<UGodfreyStageBackdropVideoComponent>())
			{
				UGodfreyStageBackdropVideoComponent* BackdropVideo =
					NewObject<UGodfreyStageBackdropVideoComponent>(Owner, TEXT("GodfreyStageBackdropVideo"));
				BackdropVideo->RegisterComponent();
				UE_LOG(LogGodfreyExhibitionQueue, Log, TEXT("Spawned GodfreyStageBackdropVideoComponent."));
			}
		}
	}
	if (bEnableGameMicrophone)
	{
		EnsureGameMicrophoneComponents();
	}
	if (bPollOnBeginPlay)
	{
		StartPolling();
	}

	EnsurePreferredPlaybackDevice();

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimerForNextTick(
			FTimerDelegate::CreateUObject(this, &UGodfreyExhibitionQueuePollComponent::BindExhibitCineCamera));
	}
#if !UE_BUILD_SHIPPING
	if (GEngine)
	{
		GEngine->ClearOnScreenDebugMessages();
	}
#endif
}

void UGodfreyExhibitionQueuePollComponent::BindExhibitCineCamera()
{
	UWorld* const World = GetWorld();
	if (!World)
	{
		return;
	}
	APlayerController* PC = World->GetFirstPlayerController();
	if (!PC)
	{
		World->GetTimerManager().SetTimerForNextTick(
			FTimerDelegate::CreateUObject(this, &UGodfreyExhibitionQueuePollComponent::BindExhibitCineCamera));
		return;
	}

	AActor* Camera = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->GetActorNameOrLabel().Equals(TEXT("Exhibit_CineCamera"), ESearchCase::IgnoreCase))
		{
			Camera = *It;
			break;
		}
	}
	if (!Camera)
	{
		UE_LOG(LogGodfreyExhibitionQueue, Warning, TEXT("Exhibit_CineCamera not found — PIE view will not match camera preview."));
		return;
	}

	PC->SetViewTarget(Camera);
	UE_LOG(LogGodfreyExhibitionQueue, Log, TEXT("SetViewTarget Exhibit_CineCamera."));
}

void UGodfreyExhibitionQueuePollComponent::EnsurePreferredPlaybackDevice()
{
	FOnAudioOutputDevicesObtained OnObtained;
	OnObtained.BindDynamic(this, &UGodfreyExhibitionQueuePollComponent::HandleAvailableAudioOutputDevices);
	UAudioMixerBlueprintLibrary::GetAvailableAudioOutputDevices(this, OnObtained);
}

void UGodfreyExhibitionQueuePollComponent::HandleAvailableAudioOutputDevices(const TArray<FAudioOutputDeviceInfo>& AvailableDevices)
{
	const FString Preferred = GetDefault<UUnrealPerformerGodfreySettings>()->GodfreyPreferredPlaybackDeviceNameFilter;
	const FAudioOutputDeviceInfo* Current = nullptr;
	const FAudioOutputDeviceInfo* PreferredMatch = nullptr;

	for (const FAudioOutputDeviceInfo& Device : AvailableDevices)
	{
		UE_LOG(LogGodfreyExhibitionQueue, Log,
			TEXT("Audio output: '%s' id=%s current=%d default=%d ch=%d sr=%d"),
			*Device.Name,
			*Device.DeviceId,
			Device.bIsCurrentDevice ? 1 : 0,
			Device.bIsSystemDefault ? 1 : 0,
			Device.NumChannels,
			Device.SampleRate);
		if (Device.bIsCurrentDevice)
		{
			Current = &Device;
		}
		if (!Preferred.IsEmpty()
			&& !PreferredMatch
			&& Device.Name.Contains(Preferred, ESearchCase::IgnoreCase))
		{
			PreferredMatch = &Device;
		}
	}

	const FString CurrentName = Current ? Current->Name : TEXT("(unknown)");
	if (Preferred.IsEmpty())
	{
		UE_LOG(LogGodfreyExhibitionQueue, Log,
			TEXT("Audio output: following Windows default ('%s'). GodfreyPreferredPlaybackDeviceNameFilter is empty."),
			*CurrentName);
		return;
	}
	if (Current && Current->Name.Contains(Preferred, ESearchCase::IgnoreCase))
	{
		UE_LOG(LogGodfreyExhibitionQueue, Log, TEXT("Audio output already matches '%s' ('%s')."), *Preferred, *CurrentName);
		return;
	}
	if (!PreferredMatch || PreferredMatch->DeviceId.IsEmpty())
	{
		UE_LOG(LogGodfreyExhibitionQueue, Error,
			TEXT("No audio output matches '%s'. Windows is using '%s'. Set the room speakers as default or change GodfreyPreferredPlaybackDeviceNameFilter."),
			*Preferred,
			*CurrentName);
		return;
	}

	UE_LOG(LogGodfreyExhibitionQueue, Log,
		TEXT("Swapping audio output '%s' -> '%s' (filter='%s')."),
		*CurrentName,
		*PreferredMatch->Name,
		*Preferred);
	FOnCompletedDeviceSwap OnSwap;
	OnSwap.BindDynamic(this, &UGodfreyExhibitionQueuePollComponent::HandlePlaybackDeviceSwap);
	UAudioMixerBlueprintLibrary::SwapAudioOutputDevice(this, PreferredMatch->DeviceId, OnSwap);
}

void UGodfreyExhibitionQueuePollComponent::HandlePlaybackDeviceSwap(const FSwapAudioOutputResult& SwapResult)
{
	if (SwapResult.Result == ESwapAudioOutputDeviceResultState::Success)
	{
		UE_LOG(LogGodfreyExhibitionQueue, Log,
			TEXT("Audio output swap succeeded (requested id=%s)."),
			*SwapResult.RequestedDeviceId);
		return;
	}
	UE_LOG(LogGodfreyExhibitionQueue, Error,
		TEXT("Audio output swap failed result=%d requested id=%s current id=%s."),
		static_cast<int32>(SwapResult.Result),
		*SwapResult.RequestedDeviceId,
		*SwapResult.CurrentDeviceId);
}

void UGodfreyExhibitionQueuePollComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopPolling();
	AbortAceStreamForEndPlay();
	ClearActiveStream();
	if (IsValid(RuntimeVoiceInput))
	{
		RuntimeVoiceInput->StopListening();
	}
	if (const UUnrealPerformerGodfreySettings* Settings = GetDefault<UUnrealPerformerGodfreySettings>())
	{
		if (Settings->bStopGodfreyBrainOnEndPlay)
		{
			FGodfreyBrainLauncher::StopOwnedProcess();
		}
	}
	Super::EndPlay(EndPlayReason);
}

void UGodfreyExhibitionQueuePollComponent::EnsureGameMicrophoneComponents()
{
	AActor* const Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	AActor* const AceCharacter = ResolveCharacterForAce();
	if (!AceCharacter)
	{
		UE_LOG(LogGodfreyExhibitionQueue, Error,
			TEXT("Game mic: cannot spawn voice path — no CharacterForAce (tag %s or BP_Godfrey_Performer)."),
			*CharacterActorTag.ToString());
		return;
	}

	UGodfreyDirectSpeechComponent* Speech = Owner->FindComponentByClass<UGodfreyDirectSpeechComponent>();
	if (!Speech)
	{
		Speech = NewObject<UGodfreyDirectSpeechComponent>(Owner, TEXT("GodfreyDirectSpeech_Runtime"));
		Speech->GodfreyBrainBaseUrl = GodfreyBrainBaseUrl;
		Speech->CharacterForAce = AceCharacter;
		Speech->CharacterActorTag = CharacterActorTag;
		Speech->AceProviderName = AceProviderName;
		Speech->StreamSampleRate = StreamSampleRate;
		Speech->StreamNumChannels = StreamNumChannels;
		Speech->bBeginThinkingOnSubmit = true;
		Speech->bReturnToListeningAfterReply = true;
		Speech->bEnableDevKeyboardSubmit = false;
		Speech->bAutoSubmitTestPromptOnBeginPlay = false;
		Speech->RegisterComponent();
		RuntimeDirectSpeech = Speech;
		UE_LOG(LogGodfreyExhibitionQueue, Log,
			TEXT("Game mic: spawned GodfreyDirectSpeech on %s → ACE character %s"),
			*Owner->GetName(),
			*AceCharacter->GetName());
	}
	else
	{
		Speech->GodfreyBrainBaseUrl = GodfreyBrainBaseUrl;
		Speech->CharacterForAce = AceCharacter;
		Speech->CharacterActorTag = CharacterActorTag;
		Speech->AceProviderName = AceProviderName;
		Speech->StreamSampleRate = StreamSampleRate;
		Speech->StreamNumChannels = StreamNumChannels;
		UE_LOG(LogGodfreyExhibitionQueue, Log,
			TEXT("Game mic: reusing existing GodfreyDirectSpeech on %s → ACE character %s"),
			*Owner->GetName(),
			*AceCharacter->GetName());
	}

	UGodfreyVoiceInputComponent* Voice = Owner->FindComponentByClass<UGodfreyVoiceInputComponent>();
	if (!Voice)
	{
		Voice = NewObject<UGodfreyVoiceInputComponent>(Owner, TEXT("GodfreyVoiceInput_Runtime"));
		Voice->GodfreyBrainBaseUrl = GodfreyBrainBaseUrl;
		Voice->DirectSpeech = Speech;
		Voice->bListenOnBeginPlay = false; // Start explicitly after RegisterComponent.
		Voice->bPauseWhileGodfreySpeaking = true;
		Voice->RegisterComponent();
		RuntimeVoiceInput = Voice;
		UE_LOG(LogGodfreyExhibitionQueue, Log,
			TEXT("Game mic: spawned GodfreyVoiceInput on %s"), *Owner->GetName());
	}
	else
	{
		Voice->GodfreyBrainBaseUrl = GodfreyBrainBaseUrl;
		Voice->DirectSpeech = Speech;
		RuntimeVoiceInput = Voice;
		UE_LOG(LogGodfreyExhibitionQueue, Log,
			TEXT("Game mic: reusing existing GodfreyVoiceInput on %s"), *Owner->GetName());
	}

	if (IsValid(Voice) && !Voice->IsListening())
	{
		Voice->StartListening();
	}

	if (!Owner->FindComponentByClass<UGodfreyListenCueComponent>())
	{
		const UUnrealPerformerGodfreySettings* Settings = GetDefault<UUnrealPerformerGodfreySettings>();
		if (!Settings || Settings->bGodfreyShowListenCueLantern)
		{
			UGodfreyListenCueComponent* Cue = NewObject<UGodfreyListenCueComponent>(Owner, TEXT("GodfreyListenCue"));
			if (Settings)
			{
				Cue->bShowCue = Settings->bGodfreyShowListenCueLantern;
				Cue->bShowLabels = Settings->bGodfreyListenCueShowLabels;
			}
			Cue->RegisterComponent();
			UE_LOG(LogGodfreyExhibitionQueue, Log, TEXT("Spawned GodfreyListenCueComponent (signal lantern)."));
		}
	}
	if (IsValid(Voice))
	{
		if (const UUnrealPerformerGodfreySettings* Settings = GetDefault<UUnrealPerformerGodfreySettings>())
		{
			Voice->PostSpeechIgnoreSeconds = Settings->GodfreyPostSpeechIgnoreSeconds;
			Voice->PreferredCaptureDeviceNameFilter = Settings->GodfreyPreferredCaptureDeviceNameFilter;
			Voice->bEnableMissedTranscriptPrompt = Settings->bGodfreyEnableMissedTranscriptPrompt;
			Voice->MissedTranscriptTimeoutSeconds = Settings->GodfreyMissedTranscriptTimeoutSeconds;
			Voice->bWarnIfSpeakWhileWait = Settings->bGodfreyWarnIfSpeakWhileWait;
			Voice->SpeakWhileWaitRmsThreshold = Settings->GodfreySpeakWhileWaitRmsThreshold;
			Voice->SpeakWhileWaitBleedHeadroom = Settings->GodfreySpeakWhileWaitBleedHeadroom;
			Voice->SpeakWhileWaitHoldSeconds = Settings->GodfreySpeakWhileWaitHoldSeconds;
			Voice->SpeakWhileWaitDisplaySeconds = Settings->GodfreySpeakWhileWaitDisplaySeconds;
			Voice->SpeakWhileWaitCooldownSeconds = Settings->GodfreySpeakWhileWaitCooldownSeconds;
			if (!Settings->GodfreyMissedTranscriptPrompt.IsEmpty())
			{
				Voice->MissedTranscriptPrompt = Settings->GodfreyMissedTranscriptPrompt;
			}
		}
	}
}

AActor* UGodfreyExhibitionQueuePollComponent::ResolveCharacterForAce() const
{
	if (IsValid(CharacterForAce))
	{
		return CharacterForAce.Get();
	}

	if (UWorld* World = GetWorld())
	{
		if (!CharacterActorTag.IsNone())
		{
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				AActor* Actor = *It;
				if (IsValid(Actor) && Actor->ActorHasTag(CharacterActorTag))
				{
					return Actor;
				}
			}
		}

		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (!IsValid(Actor))
			{
				continue;
			}
			// GetActorNameOrLabel is editor-safe; packaged builds fall back to actor name.
			if (Actor->GetActorNameOrLabel() == TEXT("BP_Godfrey_Performer")
				|| Actor->GetName().Contains(TEXT("BP_Godfrey_Performer")))
			{
				return Actor;
			}
		}
	}

	return nullptr;
}

void UGodfreyExhibitionQueuePollComponent::StartPolling()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	World->GetTimerManager().ClearTimer(PollTimerHandle);
	World->GetTimerManager().SetTimer(
		PollTimerHandle,
		FTimerDelegate::CreateUObject(this, &UGodfreyExhibitionQueuePollComponent::PollOnce),
		PollIntervalSeconds,
		true,
		PollIntervalSeconds);

	UE_LOG(LogGodfreyExhibitionQueue, Log,
		TEXT("Exhibition queue poll started (interval=%.2fs, brain=%s)"),
		PollIntervalSeconds,
		*GodfreyBrainBaseUrl);
	if (UGodfreyDiagnosticsSubsystem* Diag = UGodfreyDiagnosticsSubsystem::Get(World))
	{
		Diag->SetQueueLength(0);
	}
}

void UGodfreyExhibitionQueuePollComponent::StopPolling()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PollTimerHandle);
	}
}

void UGodfreyExhibitionQueuePollComponent::AbortAceStreamForEndPlay()
{
	if (IsValid(ActiveStreamAction))
	{
		ActiveStreamAction->Cancel();
	}
	UGodfreyPcmStreamSession::AbortActiveStreamForCharacter(ResolveCharacterForAce(), TEXT("exhibition queue EndPlay"));
}

void UGodfreyExhibitionQueuePollComponent::ClearActiveStream()
{
	if (ActiveStreamAction)
	{
		ActiveStreamAction = nullptr;
	}
	bStreamInProgress = false;
	bQueuedSpeechPlaying = false;
}

void UGodfreyExhibitionQueuePollComponent::PollOnce()
{
	if (bStreamInProgress)
	{
		return;
	}

	AActor* const AceCharacter = ResolveCharacterForAce();
	if (!AceCharacter)
	{
		UE_LOG(LogGodfreyExhibitionQueue, Warning,
			TEXT("Queue poll: no CharacterForAce (tag %s or label BP_Godfrey_Performer)"),
			*CharacterActorTag.ToString());
		return;
	}

	if (!AceCharacter->FindComponentByClass<UACEAudioCurveSourceComponent>())
	{
		UE_LOG(LogGodfreyExhibitionQueue, Warning,
			TEXT("Queue poll: %s has no ACEAudioCurveSourceComponent"),
			*AceCharacter->GetName());
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	bStreamInProgress = true;
	ActiveStreamAction = UAsyncActionStreamGodfreySpeech::PullQueuedGodfreySpeechToAudio(
		World,
		GodfreyBrainBaseUrl,
		AceCharacter,
		AceProviderName,
		StreamSampleRate,
		StreamNumChannels);

	if (!ActiveStreamAction)
	{
		UE_LOG(LogGodfreyExhibitionQueue, Error, TEXT("Queue poll: failed to create PullQueuedGodfreySpeechToAudio"));
		bStreamInProgress = false;
		return;
	}

	ActiveStreamAction->OnNoQueue.AddDynamic(this, &UGodfreyExhibitionQueuePollComponent::HandleNoQueue);
	ActiveStreamAction->OnPlaybackStarted.AddDynamic(this, &UGodfreyExhibitionQueuePollComponent::HandlePlaybackStarted);
	ActiveStreamAction->OnFinished.AddDynamic(this, &UGodfreyExhibitionQueuePollComponent::HandleFinished);
	ActiveStreamAction->OnError.AddDynamic(this, &UGodfreyExhibitionQueuePollComponent::HandleError);
	ActiveStreamAction->Activate();
}

void UGodfreyExhibitionQueuePollComponent::HandleNoQueue()
{
	bStreamInProgress = false;
	bQueuedSpeechPlaying = false;
	ActiveStreamAction = nullptr;
}

void UGodfreyExhibitionQueuePollComponent::HandlePlaybackStarted()
{
	bQueuedSpeechPlaying = true;
	AActor* const AceCharacter = ResolveCharacterForAce();
	UE_LOG(LogGodfreyExhibitionQueue, Log,
		TEXT("Queue poll: playback started on %s"),
		AceCharacter ? *AceCharacter->GetName() : TEXT("(unknown)"));
}

void UGodfreyExhibitionQueuePollComponent::HandleFinished()
{
	UE_LOG(LogGodfreyExhibitionQueue, Log, TEXT("Queue poll: stream finished"));
	bStreamInProgress = false;
	bQueuedSpeechPlaying = false;
	ActiveStreamAction = nullptr;
}

void UGodfreyExhibitionQueuePollComponent::HandleError(const FString& ErrorMessage)
{
	UE_LOG(LogGodfreyExhibitionQueue, Error, TEXT("Queue poll: %s"), *ErrorMessage);
	bStreamInProgress = false;
	bQueuedSpeechPlaying = false;
	ActiveStreamAction = nullptr;
}
