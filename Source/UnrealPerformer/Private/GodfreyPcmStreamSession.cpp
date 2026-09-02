#include "GodfreyPcmStreamSession.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Async/Async.h"

#include "ACERuntimeModule.h"
#include "ACEBlueprintLibrary.h"
#include "ACEAudioCurveSourceComponent.h"
#include "Audio2FaceParameters.h"
#include "GodfreyDiagnostics.h"
#include "UnrealPerformerGodfreySettings.h"

#include "AudioDevice.h"
#include "AudioDeviceManager.h"
#include "AudioMixerDevice.h"
#include "Components/AudioComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#if WITH_EDITOR
#include "Settings/LevelEditorMiscSettings.h"
#include "Settings/LevelEditorPlaySettings.h"
#endif
#include "Sound/SoundGroups.h"
#include "Sound/SoundWaveProcedural.h"

DEFINE_LOG_CATEGORY_STATIC(LogGodfreyPcmStream, Log, All);

namespace
{
static int32 GGodfreyUtteranceCounter = 0;

/** Bumped when parallel-audible startup logic changes — grep logs for this string to confirm binary matches source. */
static constexpr const TCHAR* GGodfreyParallelAudibleLogicStamp = TEXT("godfrey-parallel-audible-2026-07-30-v10-audio-thread-safe");

/**
 * ACE BufferLengthInSeconds keeps that much audio in the device after the procedural wall
 * clock reaches sent PCM. Flushing/muting at Wall≈Sent therefore cuts the last ~0.35s
 * (Welcome 2026-08-31 Wall=7.66 Sent=7.61 mute+hitch on the closing question).
 */
static float GodfreyAceAudibleTailSlackSeconds()
{
	const UUnrealPerformerGodfreySettings* Settings = GetDefault<UUnrealPerformerGodfreySettings>();
	float BufferSec = 0.35f;
	if (Settings && Settings->bApplyGodfreyAceBufferLength)
	{
		BufferSec = FMath::Clamp(Settings->GodfreyAceBufferLengthSeconds, 0.05f, 1.5f);
	}
	constexpr float DacSlackSec = 0.08f;
	return BufferSec + DacSlackSec;
}

/**
 * Extra hush after BufferLength has drained. ACE's wall clock leads the speakers;
 * muting when Wall said "hush" (2026-09-01 09:39) hard-cut the last sentence.
 * Do not mute/Stop on this timer. IsProceduralAudioPlaying() stays true until Stop.
 */
static float GodfreyAceEndAudioPostRollSeconds()
{
	const UUnrealPerformerGodfreySettings* Settings = GetDefault<UUnrealPerformerGodfreySettings>();
	constexpr float FallbackSec = 1.5f;
	if (!Settings)
	{
		return FallbackSec;
	}
	return FMath::Clamp(Settings->GodfreyAceEndAudioPostRollSeconds, 0.15f, 2.5f);
}

/**
 * Last-voice RMS floor. 16% of utterance peak (min 4500) treated quieter last syllables as hush
 * (2026-09-01 21:09 utt-14 LastVoice=14.61 LastGate=17.35 — mouth rest 2.7s before the last word).
 * Peak-relative floors also rise after a shout, so later calm speech never updates LastVoice.
 * Cap so a loud peak cannot blank the ending; stay above gate-750 breath.
 */
static int32 GodfreyLastVoiceRmsFloor(const int32 PeakAbs, const int32 SilenceGate)
{
	const int32 GateFloor = FMath::Max(1800, FMath::Max(1, SilenceGate) * 2);
	const int32 PctFloor = (FMath::Max(0, PeakAbs) * 8) / 100;
	return FMath::Clamp(PctFloor, GateFloor, 2800);
}

static bool GodfreyAceAudibleTailHasDrained(const float EffectiveWallSeconds, const float SentAudioSeconds)
{
	if (EffectiveWallSeconds < 0.f)
	{
		return false;
	}
	if (SentAudioSeconds <= 0.25f)
	{
		return EffectiveWallSeconds >= GodfreyAceAudibleTailSlackSeconds();
	}
	return EffectiveWallSeconds >= (SentAudioSeconds + GodfreyAceAudibleTailSlackSeconds() + GodfreyAceEndAudioPostRollSeconds());
}

/**
 * FAudioDevice::GetTransientPrimaryVolume() asserts IsInAudioThread().
 * Speak-stream diagnostics / timers run on the game thread (and crash Standalone/Game on FinishStream).
 * Returns -1 when not readable from this thread; SetTransientPrimaryVolume remains game-thread safe.
 */
static float SafeGetTransientPrimaryVolume(const FAudioDevice* AudioDevice)
{
	if (!AudioDevice || !IsInAudioThread())
	{
		return -1.f;
	}
	return AudioDevice->GetTransientPrimaryVolume();
}

/** PrimaryVolume = TransientPrimaryVolume * FApp::GetVolumeMultiplier(). AppMult=0 when the editor is unfocused. */
static bool TryRestorePieAudibilityIfSilent(UWorld* World, const TCHAR* Context)
{
	if (!World || !World->IsPlayInEditor())
	{
		return false;
	}

	const FAudioDeviceHandle DeviceHandle = World->GetAudioDevice();
	FAudioDevice* AudioDevice = DeviceHandle.GetAudioDevice();
	if (!AudioDevice)
	{
		return false;
	}

	const float PrimaryVol = AudioDevice->GetPrimaryVolume();
	const float TransientVol = SafeGetTransientPrimaryVolume(AudioDevice);
	const float AppMult = FApp::GetVolumeMultiplier();
	const float UnfocusedMult = FApp::GetUnfocusedVolumeMultiplier();
	const bool bDeviceMuted = AudioDevice->IsAudioDeviceMuted();

	if (PrimaryVol > KINDA_SMALL_NUMBER && !bDeviceMuted && UnfocusedMult > KINDA_SMALL_NUMBER)
	{
		return false;
	}

	bool bRestored = false;

	// Focus pump re-applies UnfocusedVolumeMultiplier every frame — pin it to 1 so AppMult stays audible.
	if (UnfocusedMult < 1.f - KINDA_SMALL_NUMBER)
	{
		UE_LOG(LogGodfreyPcmStream, Warning,
			TEXT("[Godfrey audible] UnfocusedVolumeMultiplier was %.3f at '%s' — forcing 1.0 so PIE stays audible without viewport focus."),
			UnfocusedMult,
			Context);
		FApp::SetUnfocusedVolumeMultiplier(1.f);
		bRestored = true;
	}

	if (AppMult <= KINDA_SMALL_NUMBER)
	{
		UE_LOG(LogGodfreyPcmStream, Warning,
			TEXT("[Godfrey audible] FApp::GetVolumeMultiplier() is 0 at '%s'. PrimaryVol=%.3f TransientVol=%.3f Unfocused=%.3f — restoring app volume to 1 (also needs Editor Allow Background Audio)."),
			Context,
			PrimaryVol,
			TransientVol,
			UnfocusedMult);
#if WITH_EDITOR
		if (ULevelEditorMiscSettings* Misc = GetMutableDefault<ULevelEditorMiscSettings>())
		{
			Misc->bAllowBackgroundAudio = true;
			Misc->EditorVolumeLevel = 1.f;
		}
#endif
		FApp::SetVolumeMultiplier(1.f);
		bRestored = true;
	}

	// TransientVol < 0 means unread (wrong thread); fall back to PrimaryVol for the restore decision.
	const bool bTransientKnownSilent = TransientVol >= 0.f && TransientVol <= KINDA_SMALL_NUMBER;
	if (bTransientKnownSilent || PrimaryVol <= KINDA_SMALL_NUMBER)
	{
		AudioDevice->SetTransientPrimaryVolume(1.f);
		bRestored = true;
	}

	if (bDeviceMuted)
	{
		AudioDevice->SetDeviceMuted(false);
		bRestored = true;
	}

	if (!bRestored)
	{
		UE_LOG(LogGodfreyPcmStream, Warning,
			TEXT("[Godfrey audible] PIE output still silent at '%s' (PrimaryVol=%.3f TransientVol=%.3f AppMult=%.3f Unfocused=%.3f DeviceMuted=%d). Check PIE viewport volume slider."),
			Context,
			PrimaryVol,
			TransientVol,
			AppMult,
			UnfocusedMult,
			bDeviceMuted ? 1 : 0);
	}

	return bRestored;
}

static void GodfreyAudibleSelfTest()
{
	UWorld* World = nullptr;
	if (GEngine)
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (UWorld* Candidate = Context.World())
			{
				if (Candidate->WorldType == EWorldType::PIE || Candidate->WorldType == EWorldType::Game)
				{
					World = Candidate;
					break;
				}
			}
		}
	}

	if (!World)
	{
		UE_LOG(LogGodfreyPcmStream, Warning, TEXT("godfrey.AudibleSelfTest: no PIE/game world — start PIE first."));
		return;
	}

	TryRestorePieAudibilityIfSilent(World, TEXT("AudibleSelfTest"));

	constexpr int32 SampleRate = 48000;
	constexpr float DurationSec = 1.5f;
	constexpr float FrequencyHz = 440.f;
	const int32 NumFrames = FMath::RoundToInt(SampleRate * DurationSec);
	TArray<uint8> Pcm;
	Pcm.SetNumUninitialized(NumFrames * sizeof(int16));
	int16* Samples = reinterpret_cast<int16*>(Pcm.GetData());
	for (int32 FrameIndex = 0; FrameIndex < NumFrames; ++FrameIndex)
	{
		const float T = static_cast<float>(FrameIndex) / static_cast<float>(SampleRate);
		Samples[FrameIndex] = static_cast<int16>(32767.f * 0.35f * FMath::Sin(2.f * UE_PI * FrequencyHz * T));
	}

	USoundWaveProcedural* Sound = NewObject<USoundWaveProcedural>(World);
	Sound->SetSampleRate(SampleRate);
	Sound->NumChannels = 1;
	Sound->Duration = DurationSec;
	Sound->TotalSamples = NumFrames;
	Sound->RawPCMDataSize = Pcm.Num();
	Sound->bLooping = false;
	Sound->SoundGroup = SOUNDGROUP_Default;
	Sound->VirtualizationMode = EVirtualizationMode::PlayWhenSilent;
	Sound->QueueAudio(Pcm.GetData(), Pcm.Num());

	FVector PlayLocation = FVector::ZeroVector;
	if (APlayerController* PC = World->GetFirstPlayerController())
	{
		FRotator PlayRotation;
		PC->GetPlayerViewPoint(PlayLocation, PlayRotation);
	}

	// One procedural wave must not feed two AudioComponents — that races the PCM queue and can crash AudioMixer.
	if (UAudioComponent* AC = UGameplayStatics::SpawnSoundAtLocation(
			World,
			Sound,
			PlayLocation,
			FRotator::ZeroRotator,
			1.f,
			1.f,
			0.f,
			nullptr,
			nullptr,
			true))
	{
		AC->bIsUISound = false;
		AC->bAllowSpatialization = false;
		AC->SetVolumeMultiplier(1.f);
		UE_LOG(LogGodfreyPcmStream, Log,
			TEXT("godfrey.AudibleSelfTest: SpawnSoundAtLocation AC=%p IsPlaying=%d Loc=%s (~%.1fs, %s)."),
			AC,
			AC->IsPlaying() ? 1 : 0,
			*PlayLocation.ToString(),
			DurationSec,
			GGodfreyParallelAudibleLogicStamp);
	}
	else
	{
		UE_LOG(LogGodfreyPcmStream, Warning, TEXT("godfrey.AudibleSelfTest: SpawnSoundAtLocation failed."));
	}
}

static FAutoConsoleCommand CCmdGodfreyAudibleSelfTest(
	TEXT("godfrey.AudibleSelfTest"),
	TEXT("PIE: play 440Hz test tone via SpawnSoundAtLocation (single procedural player). If you hear nothing, the issue is editor/OS audio routing, not Godfrey PCM."),
	FConsoleCommandDelegate::CreateStatic(&GodfreyAudibleSelfTest));

/** One active ingest/playback session per character; superseded sessions must unbind ACE delegates. */
static TMap<TWeakObjectPtr<AActor>, TWeakObjectPtr<UGodfreyPcmStreamSession>> GActiveGodfreyAceSessionByCharacter;

static int32 ComputeMaxPcmBytesPerAceSubChunk(int32 SampleRate, int32 NumChannels, float ChunkDurationMs)
{
	const int32 FrameSize = NumChannels * static_cast<int32>(sizeof(int16));
	if (FrameSize <= 0 || SampleRate <= 0)
	{
		return FrameSize;
	}
	const float ClampedMs = FMath::Clamp(ChunkDurationMs, 10.f, 80.f);
	const int32 FramesPerSubChunk = FMath::Max(1, FMath::RoundToInt(static_cast<float>(SampleRate) * (ClampedMs / 1000.f)));
	const int32 UnalignedBytes = FramesPerSubChunk * FrameSize;
	return FMath::Max(FrameSize, (UnalignedBytes / FrameSize) * FrameSize);
}

static void ComputePcm16PeakRms(const TArray<uint8>& PcmBytes, int32 NumChannels, int16& OutPeak, float& OutRms)
{
	OutPeak = 0;
	OutRms = 0.f;
	const int32 FrameSize = NumChannels * static_cast<int32>(sizeof(int16));
	if (PcmBytes.Num() < FrameSize || (PcmBytes.Num() % FrameSize) != 0)
	{
		return;
	}

	const int32 NumSamples = PcmBytes.Num() / static_cast<int32>(sizeof(int16));
	const int16* Samples = reinterpret_cast<const int16*>(PcmBytes.GetData());
	int64 SumSquares = 0;
	for (int32 SampleIndex = 0; SampleIndex < NumSamples; ++SampleIndex)
	{
		const int16 Sample = Samples[SampleIndex];
		OutPeak = static_cast<int16>(FMath::Max(static_cast<int32>(OutPeak), FMath::Abs(static_cast<int32>(Sample))));
		SumSquares += static_cast<int64>(Sample) * static_cast<int64>(Sample);
	}
	OutRms = (NumSamples > 0) ? FMath::Sqrt(static_cast<float>(SumSquares) / static_cast<float>(NumSamples)) : 0.f;
}

static const TCHAR* AudioComponentPlayStateToString(const EAudioComponentPlayState PlayState)
{
	switch (PlayState)
	{
	case EAudioComponentPlayState::Playing: return TEXT("Playing");
	case EAudioComponentPlayState::Stopped: return TEXT("Stopped");
	case EAudioComponentPlayState::Paused: return TEXT("Paused");
	case EAudioComponentPlayState::FadingIn: return TEXT("FadingIn");
	case EAudioComponentPlayState::FadingOut: return TEXT("FadingOut");
	default: return TEXT("Unknown");
	}
}
} // namespace

void UGodfreyPcmStreamSession::SetClientRequestT0PlatformSeconds(double PlatformSeconds)
{
	ClientRequestT0PlatformSeconds = PlatformSeconds;
}

void UGodfreyPcmStreamSession::SetBrainRequestId(const FString& InRequestId)
{
	BrainRequestId = InRequestId;
}

void UGodfreyPcmStreamSession::SetHoldAudibleUntilReleased(bool bHold)
{
	bHoldAudibleUntilReleased = bHold;
	if (!bHold)
	{
		return;
	}

	AActor* Character = TargetCharacter.Get();
	UACEAudioCurveSourceComponent* AceComp = Character
		? Character->FindComponentByClass<UACEAudioCurveSourceComponent>()
		: nullptr;
	if (!AceComp)
	{
		return;
	}

	const UUnrealPerformerGodfreySettings* Settings = GetDefault<UUnrealPerformerGodfreySettings>();
	const float Gate = Settings ? Settings->GodfreyAceHoldPlayMinCurveTimestampGate : 99999.f;
	if (!bGodfreySavedAceMinCurveLead)
	{
		GodfreySavedAceMinCurveTimestampBeforePlay = AceComp->MinCurveTimestampSecondsBeforePlay;
		bGodfreySavedAceMinCurveLead = true;
		bGodfreyAcePrimingApplied = true;
		bGodfreyAceMinCurveLeadOverriddenThisUtterance = true;
	}
	AceComp->MinCurveTimestampSecondsBeforePlay = Gate;
	UE_LOG(LogGodfreyPcmStream, Log,
		TEXT("Godfrey utterance %d: hold audible until released (MinCurveTimestampSecondsBeforePlay -> %.1f) — prefetch ingest, no Play."),
		UtteranceOrdinal,
		Gate);
}

void UGodfreyPcmStreamSession::ReleaseAudibleHold(const TCHAR* Reason)
{
	if (!bHoldAudibleUntilReleased)
	{
		return;
	}
	bHoldAudibleUntilReleased = false;

	AActor* Character = TargetCharacter.Get();
	UACEAudioCurveSourceComponent* AceComp = Character
		? Character->FindComponentByClass<UACEAudioCurveSourceComponent>()
		: nullptr;
	if (!AceComp)
	{
		return;
	}

	const UUnrealPerformerGodfreySettings* Settings = GetDefault<UUnrealPerformerGodfreySettings>();
	float Target = 0.f;
	if (Settings && Settings->GodfreyAceMinCurveTimestampBeforePlay >= 0.f)
	{
		Target = Settings->GodfreyAceMinCurveTimestampBeforePlay;
	}
	AceComp->MinCurveTimestampSecondsBeforePlay = Target;
	if (bFinished)
	{
		DeferredUnbindFinishStreamPlatformSeconds = FPlatformTime::Seconds();
	}
	UE_LOG(LogGodfreyPcmStream, Log,
		TEXT("Godfrey utterance %d: released audible hold (%s) — MinCurveTimestampSecondsBeforePlay -> %.4f SamplesSentToACE=%lld."),
		UtteranceOrdinal,
		Reason ? Reason : TEXT("release"),
		Target,
		TotalSamplesSentToAce);
}

void UGodfreyPcmStreamSession::ReleaseAudibleHoldForCharacter(AActor* Character, const FString& Reason)
{
	if (!IsValid(Character))
	{
		return;
	}

	const TWeakObjectPtr<AActor> Key(Character);
	if (const TWeakObjectPtr<UGodfreyPcmStreamSession>* Existing = GActiveGodfreyAceSessionByCharacter.Find(Key))
	{
		if (UGodfreyPcmStreamSession* Session = Existing->Get())
		{
			Session->ReleaseAudibleHold(*Reason);
		}
	}
}

void UGodfreyPcmStreamSession::NotifyFirstHttpBodyBytesPlatformSeconds(double PlatformSeconds)
{
	if (FirstHttpBodyBytesPlatformSeconds < 0.0)
	{
		FirstHttpBodyBytesPlatformSeconds = PlatformSeconds;
	}
	LastHttpBodyProgressPlatformSeconds = FPlatformTime::Seconds();
}

void UGodfreyPcmStreamSession::NotifyHttpBodyProgress()
{
	LastHttpBodyProgressPlatformSeconds = FPlatformTime::Seconds();
}

void UGodfreyPcmStreamSession::NotifyHttpReceiveComplete()
{
	bHttpReceiveCompleted = true;
}

void UGodfreyPcmStreamSession::BeginDestroy()
{
	CancelDeferredAceUnbind();
	CancelAudibleDiagnosticsTimer();
	UnbindParallelAudibleUnderflowDelegate();
	StopParallelAudiblePlayback(true);
	UnregisterActiveAceSessionForCharacter();
	UnbindAceDelegates();
	Super::BeginDestroy();
}

bool UGodfreyPcmStreamSession::IsCharacterAudiblePlaybackActive(const AActor* Character)
{
	if (!IsValid(Character))
	{
		return false;
	}

	const TWeakObjectPtr<AActor> Key(const_cast<AActor*>(Character));
	if (const TWeakObjectPtr<UGodfreyPcmStreamSession>* Existing = GActiveGodfreyAceSessionByCharacter.Find(Key))
	{
		if (const UGodfreyPcmStreamSession* Session = Existing->Get())
		{
			return !Session->HasAcePlaybackEnded();
		}
	}
	return false;
}

void UGodfreyPcmStreamSession::RegisterAsActiveAceSessionForCharacter()
{
	AActor* Character = TargetCharacter.Get();
	if (!Character)
	{
		return;
	}

	const TWeakObjectPtr<AActor> Key(Character);
	if (const TWeakObjectPtr<UGodfreyPcmStreamSession>* Existing = GActiveGodfreyAceSessionByCharacter.Find(Key))
	{
		if (UGodfreyPcmStreamSession* Prior = Existing->Get())
		{
			if (Prior != this)
			{
				UE_LOG(LogGodfreyPcmStream, Warning,
					TEXT("Godfrey utterance %d: superseding prior utterance %d on %s — unbinding stale ACE delegates to prevent cross-talk."),
					UtteranceOrdinal,
					Prior->UtteranceOrdinal,
					*Character->GetName());
				Prior->CancelDeferredAceUnbind();
				Prior->StopParallelAudiblePlayback(true);
				Prior->UnbindAceDelegates();
			}
		}
	}

	GActiveGodfreyAceSessionByCharacter.Add(Key, this);
}

void UGodfreyPcmStreamSession::UnregisterActiveAceSessionForCharacter()
{
	AActor* Character = TargetCharacter.Get();
	if (!Character)
	{
		return;
	}

	const TWeakObjectPtr<AActor> Key(Character);
	if (const TWeakObjectPtr<UGodfreyPcmStreamSession>* Existing = GActiveGodfreyAceSessionByCharacter.Find(Key))
	{
		if (Existing->Get() == this)
		{
			GActiveGodfreyAceSessionByCharacter.Remove(Key);
		}
	}
}

bool UGodfreyPcmStreamSession::IsActiveAceSessionForCharacter() const
{
	AActor* Character = TargetCharacter.Get();
	if (!Character)
	{
		return false;
	}

	const TWeakObjectPtr<AActor> Key(Character);
	if (const TWeakObjectPtr<UGodfreyPcmStreamSession>* Existing = GActiveGodfreyAceSessionByCharacter.Find(Key))
	{
		return Existing->Get() == this;
	}

	return false;
}

void UGodfreyPcmStreamSession::UnbindAceDelegates()
{
	if (AActor* Character = TargetCharacter.Get())
	{
		if (UACEAudioCurveSourceComponent* AceComp = Character->FindComponentByClass<UACEAudioCurveSourceComponent>())
		{
			if (bBoundAceAnimationStarted)
			{
				AceComp->OnAnimationStarted.RemoveDynamic(this, &UGodfreyPcmStreamSession::HandleAceAnimationStarted);
			}
			if (bBoundAceAnimationEnded)
			{
				AceComp->OnAnimationEnded.RemoveDynamic(this, &UGodfreyPcmStreamSession::HandleAceAnimationEnded);
			}
		}
	}
	bBoundAceAnimationStarted = false;
	bBoundAceAnimationEnded = false;
}

void UGodfreyPcmStreamSession::CancelDeferredAceUnbind()
{
	if (UWorld* World = DeferredAceUnbindWorld.Get())
	{
		World->GetTimerManager().ClearTimer(DeferredAceUnbindTimerHandle);
	}
	DeferredAceUnbindWorld.Reset();
	DeferredAceUnbindTimerHandle.Invalidate();
	bDeferredAceUnbindActive = false;
	DeferredUnbindFinishStreamPlatformSeconds = 0.0;
}

void UGodfreyPcmStreamSession::ScheduleDeferredAceUnbindAfterFinishStream(UWorld* World, const double FinishStreamPlatformSeconds)
{
	if (!World || (!bBoundAceAnimationStarted && !bBoundAceAnimationEnded))
	{
		return;
	}

	if (UtteranceStartupMetrics.bAceOnAnimationStartedObserved && bAcePlaybackEndedObserved)
	{
		UnbindAceDelegates();
		return;
	}

	DeferredUnbindFinishStreamPlatformSeconds = FinishStreamPlatformSeconds;
	DeferredUnbindStartPlatformSeconds = FPlatformTime::Seconds();
	DeferredUnbindUtteranceOrdinal = UtteranceOrdinal;

	const float ExpectedSec = (StreamSampleRate > 0)
		? static_cast<float>(TotalSamplesSentToAce) / static_cast<float>(StreamSampleRate)
		: 0.f;
	float RemainingSec = ExpectedSec;
	if (AActor* Character = TargetCharacter.Get())
	{
		if (UACEAudioCurveSourceComponent* AceComp = Character->FindComponentByClass<UACEAudioCurveSourceComponent>())
		{
			const float Wall = AceComp->GetProceduralPlaybackWallClockSeconds();
			RemainingSec = FMath::Max(0.f, ExpectedSec - Wall);
		}
	}

	UE_LOG(LogGodfreyPcmStream, Log,
		TEXT("Godfrey utterance %d: ACE delegates left bound until OnAnimationEnded (ingest complete; playback may continue ~%.1fs)."),
		UtteranceOrdinal,
		RemainingSec);

	EnsureAudioEndWatchdogScheduled(World);
}

void UGodfreyPcmStreamSession::EnsureAudioEndWatchdogScheduled(UWorld* World)
{
	if (!World || (!bBoundAceAnimationStarted && !bBoundAceAnimationEnded))
	{
		return;
	}

	if (bAcePlaybackEndedObserved)
	{
		return;
	}

	if (bDeferredAceUnbindActive
		&& DeferredAceUnbindWorld.Get() == World
		&& DeferredUnbindUtteranceOrdinal == UtteranceOrdinal)
	{
		return;
	}

	// Preserve FinishStream timestamp across reschedule — CancelDeferredAceUnbind clears it and
	// would permanently disable never-started / hard-timeout failovers (mic stays paused forever).
	const double PreservedFinishStreamPlatformSeconds = DeferredUnbindFinishStreamPlatformSeconds;
	const double PreservedStartPlatformSeconds = DeferredUnbindStartPlatformSeconds;

	CancelDeferredAceUnbind();

	DeferredUnbindFinishStreamPlatformSeconds = PreservedFinishStreamPlatformSeconds;
	DeferredUnbindStartPlatformSeconds = PreservedStartPlatformSeconds;

	bDeferredAceUnbindActive = true;
	DeferredUnbindUtteranceOrdinal = UtteranceOrdinal;
	if (DeferredUnbindStartPlatformSeconds <= 0.0)
	{
		DeferredUnbindStartPlatformSeconds = FPlatformTime::Seconds();
	}
	DeferredAceUnbindWorld = World;

	World->GetTimerManager().SetTimer(
		DeferredAceUnbindTimerHandle,
		FTimerDelegate::CreateUObject(this, &UGodfreyPcmStreamSession::ProcessDeferredAceUnbindTick),
		0.05f,
		true);
}

void UGodfreyPcmStreamSession::ProcessDeferredAceUnbindTick()
{
	if (!bDeferredAceUnbindActive || (!bBoundAceAnimationStarted && !bBoundAceAnimationEnded))
	{
		CancelDeferredAceUnbind();
		return;
	}

	if (UtteranceOrdinal != DeferredUnbindUtteranceOrdinal)
	{
		CancelDeferredAceUnbind();
		return;
	}

	const float GraceSeconds = GetDefault<UUnrealPerformerGodfreySettings>()->GodfreyAcePostFinishOnAnimationStartedDelegateGraceSeconds;
	const double Now = FPlatformTime::Seconds();

	if (bHoldAudibleUntilReleased)
	{
		return;
	}

	if (bAcePlaybackEndedObserved)
	{
		CancelDeferredAceUnbind();
		UnbindAceDelegates();
		return;
	}

	// Fail fast if ACE never starts audible/lipsync clock after ingest — no silent parallel substitute.
	// Cap never-started wait so mic/lantern cannot stay Wait for the full 10s post-finish grace.
	const float NeverStartedGraceSeconds = FMath::Clamp(GraceSeconds, 0.5f, 2.0f);
	if (bFinished
		&& !UtteranceStartupMetrics.bAceOnAnimationStartedObserved
		&& NeverStartedGraceSeconds > 0.f
		&& DeferredUnbindFinishStreamPlatformSeconds > 0.0
		&& (Now - DeferredUnbindFinishStreamPlatformSeconds) >= static_cast<double>(NeverStartedGraceSeconds))
	{
		const FString Err = FString::Printf(
			TEXT("Godfrey utterance %d FAILED: ACE OnAnimationStarted never fired within %.1fs after FinishStream (ACE-only single clock; no parallel fallback). Check ACE Play()/BufferLength/MinBlendShapeSamples."),
			DeferredUnbindUtteranceOrdinal,
			NeverStartedGraceSeconds);
		UE_LOG(LogGodfreyPcmStream, Error, TEXT("%s"), *Err);
		ReportError(Err);
		OnPlaybackEnded.Broadcast();
		bAcePlaybackEndedObserved = true;
		CancelDeferredAceUnbind();
		UnbindAceDelegates();
		return;
	}

	// Prefer ending when ACE's own playback wall catches MaxCurveTs / PCM duration — OnAnimationEnded is unreliable.
	if (TryCompleteAcePlaybackFromAudioEndWatchdog())
	{
		return;
	}

	// Hard timeout only after FinishStream (ingest complete).
	if (!bFinished || DeferredUnbindFinishStreamPlatformSeconds <= 0.0)
	{
		return;
	}

	const double ExpectedPlaybackSec = (StreamSampleRate > 0)
		? static_cast<double>(TotalSamplesSentToAce) / static_cast<double>(StreamSampleRate)
		: 30.0;
	const double TimeoutSec = FMath::Max(static_cast<double>(GraceSeconds), ExpectedPlaybackSec + 5.0);
	const double Elapsed = Now - DeferredUnbindFinishStreamPlatformSeconds;

	if (Elapsed < TimeoutSec)
	{
		return;
	}

	UE_LOG(LogGodfreyPcmStream, Error,
		TEXT("Godfrey utterance %d FAILED: OnAnimationEnded did not arrive within %.1fs (expected playback ~%.1fs); forcing OnPlaybackEnded — investigate ACE hang (no silent fallback)."),
		DeferredUnbindUtteranceOrdinal,
		Elapsed,
		ExpectedPlaybackSec);

	CompleteAcePlaybackEnded(TEXT("hard-timeout (OnAnimationEnded never arrived)"));
}

void UGodfreyPcmStreamSession::ApplyGodfreyAcePlaybackPriming(UACEAudioCurveSourceComponent* AceComp)
{
	if (!AceComp)
	{
		return;
	}

	const UUnrealPerformerGodfreySettings* Settings = GetDefault<UUnrealPerformerGodfreySettings>();
	if (Settings->bApplyGodfreyAceBufferLength)
	{
		GodfreySavedAceBufferLengthInSeconds = AceComp->BufferLengthInSeconds;
		bGodfreySavedAceBufferLength = true;
		const float Clamped = FMath::Clamp(Settings->GodfreyAceBufferLengthSeconds, 0.05f, 1.5f);
		AceComp->BufferLengthInSeconds = Clamped;
		bGodfreyAcePrimingApplied = true;
		bGodfreyAceBufferLengthOverriddenThisUtterance = true;
		UE_LOG(LogGodfreyPcmStream, Log,
			TEXT("Godfrey ACE priming: BufferLengthInSeconds %.4f -> %.4f (Project Settings / Test Live Audio Godfrey)"),
			GodfreySavedAceBufferLengthInSeconds,
			Clamped);
	}

	if (Settings->GodfreyAceMinBlendShapeSamplesOverride >= 0)
	{
		GodfreySavedAceMinBlendShapeSamplesBeforePlay = AceComp->MinBlendShapeSamplesBeforePlay;
		bGodfreySavedAceMinBlend = true;
		AceComp->MinBlendShapeSamplesBeforePlay = Settings->GodfreyAceMinBlendShapeSamplesOverride;
		bGodfreyAcePrimingApplied = true;
		bGodfreyAceMinBlendOverriddenThisUtterance = true;
		UE_LOG(LogGodfreyPcmStream, Log,
			TEXT("Godfrey ACE priming: MinBlendShapeSamplesBeforePlay %d -> %d"),
			GodfreySavedAceMinBlendShapeSamplesBeforePlay,
			AceComp->MinBlendShapeSamplesBeforePlay);
	}

	if (Settings->bGodfreyAceHoldPlayUntilStreamEnd || bHoldAudibleUntilReleased)
	{
		GodfreySavedAceMinCurveTimestampBeforePlay = AceComp->MinCurveTimestampSecondsBeforePlay;
		bGodfreySavedAceMinCurveLead = true;
		AceComp->MinCurveTimestampSecondsBeforePlay = Settings->GodfreyAceHoldPlayMinCurveTimestampGate;
		bGodfreyAcePrimingApplied = true;
		bGodfreyAceMinCurveLeadOverriddenThisUtterance = true;
		if (bHoldAudibleUntilReleased)
		{
			UE_LOG(LogGodfreyPcmStream, Log,
				TEXT("Godfrey ACE priming: hold Play until released (arrival-card prefetch) — MinCurveTimestampSecondsBeforePlay %.4f -> %.1f."),
				GodfreySavedAceMinCurveTimestampBeforePlay,
				AceComp->MinCurveTimestampSecondsBeforePlay);
		}
		else
		{
			UE_LOG(LogGodfreyPcmStream, Warning,
				TEXT("Godfrey ACE priming: hold Play until stream end ENABLED — MinCurveTimestampSecondsBeforePlay %.4f -> %.1f. ")
				TEXT("Best lip sync on long clips; startup waits for full HTTP download + A2F burst (~seconds on long replies). ")
				TEXT("Disable bGodfreyAceHoldPlayUntilStreamEnd in Project Settings for faster time-to-first-word."),
				GodfreySavedAceMinCurveTimestampBeforePlay,
				AceComp->MinCurveTimestampSecondsBeforePlay);
		}
	}
	else if (Settings->GodfreyAceMinCurveTimestampBeforePlay >= 0.f)
	{
		GodfreySavedAceMinCurveTimestampBeforePlay = AceComp->MinCurveTimestampSecondsBeforePlay;
		bGodfreySavedAceMinCurveLead = true;
		AceComp->MinCurveTimestampSecondsBeforePlay = Settings->GodfreyAceMinCurveTimestampBeforePlay;
		bGodfreyAcePrimingApplied = true;
		bGodfreyAceMinCurveLeadOverriddenThisUtterance = true;
		UE_LOG(LogGodfreyPcmStream, Log,
			TEXT("Godfrey ACE priming: early Play — MinCurveTimestampSecondsBeforePlay %.4f -> %.4f (faster startup; extrapolation covers brief curve gaps on long monolithic streams)"),
			GodfreySavedAceMinCurveTimestampBeforePlay,
			AceComp->MinCurveTimestampSecondsBeforePlay);
	}
}

float UGodfreyPcmStreamSession::GetUnmatchedAudioSeconds() const
{
	if (!bStreamStarted || StreamSampleRate <= 0)
	{
		return 0.f;
	}

	const AActor* Character = TargetCharacter.Get();
	if (!Character)
	{
		return 0.f;
	}

	const UACEAudioCurveSourceComponent* AceComp = Character->FindComponentByClass<UACEAudioCurveSourceComponent>();
	if (!AceComp)
	{
		return 0.f;
	}

	const float SentAudioSec = static_cast<float>(TotalSamplesSentToAce) / static_cast<float>(StreamSampleRate);
	const float MaxCurveTs = FMath::Max(0.f, AceComp->GetMaxReceivedCurveTimestamp());
	return SentAudioSec - MaxCurveTs;
}

float UGodfreyPcmStreamSession::GetSentAudioSeconds() const
{
	if (!bStreamStarted || StreamSampleRate <= 0)
	{
		return 0.f;
	}
	return static_cast<float>(TotalSamplesSentToAce) / static_cast<float>(StreamSampleRate);
}

float UGodfreyPcmStreamSession::GetPlaybackWallSeconds() const
{
	const AActor* Character = TargetCharacter.Get();
	const UACEAudioCurveSourceComponent* AceComp = Character
		? Character->FindComponentByClass<UACEAudioCurveSourceComponent>()
		: nullptr;
	if (!AceComp)
	{
		return -1.f;
	}
	return AceComp->GetProceduralPlaybackWallClockSeconds();
}

float UGodfreyPcmStreamSession::GetLastVoiceAudioSeconds() const
{
	if (!bStreamStarted || StreamSampleRate <= 0 || LastNonSilentSamplesSentToAce <= 0)
	{
		return 0.f;
	}
	return static_cast<float>(LastNonSilentSamplesSentToAce) / static_cast<float>(StreamSampleRate);
}

bool UGodfreyPcmStreamSession::ShouldDeferEndAudioSamplesForCurveCatchUp() const
{
	// 2026-09-01 11:22: waiting until Wall was 2s past Sent still glitched. A2F withholds
	// ~0.6–0.9s of curves until EndAudioSamples, so the last word hit Low curve lead /
	// Extrapolating, then the game thread hitch landed 2s later. NVIDIA marks
	// EndAudioSamples safe on any thread — FinishStream dispatches it at HTTP drain.
	return false;
}

bool UGodfreyPcmStreamSession::ShouldForceEndAudioSamplesDespiteCatchUpLag(const float CatchUpElapsedSeconds) const
{
	if (bAcePlaybackEndedObserved)
	{
		return true;
	}

	const AActor* Character = TargetCharacter.Get();
	const UACEAudioCurveSourceComponent* AceComp = Character
		? Character->FindComponentByClass<UACEAudioCurveSourceComponent>()
		: nullptr;

	const float Sent = GetSentAudioSeconds();
	const float Wall = GetPlaybackWallSeconds();
	const float EffectiveWall = (Wall >= 0.f) ? Wall : LastPositiveProceduralWallSeconds;

	// Playing flag stuck true is normal — flush once the playhead is in trailing hush.
	if (GodfreyAceAudibleTailHasDrained(EffectiveWall, Sent))
	{
		return true;
	}

	const bool bPlaying = AceComp && AceComp->IsProceduralAudioPlaying();
	if (!bPlaying && Sent > 0.25f && EffectiveWall >= (Sent * 0.92f))
	{
		return true;
	}

	constexpr float AbsoluteSafetySeconds = 120.f;
	if (CatchUpElapsedSeconds >= AbsoluteSafetySeconds)
	{
		return true;
	}

	return false;
}

int32 UGodfreyPcmStreamSession::GetEffectiveIngestPushBudget(int32 ConfigBudget, bool bAllowOverrun) const
{
	const int32 ClampedConfigBudget = FMath::Max(1, ConfigBudget);
	if (bAllowOverrun || !bStreamStarted || bFinished)
	{
		return ClampedConfigBudget;
	}

	const UUnrealPerformerGodfreySettings* Settings = GetDefault<UUnrealPerformerGodfreySettings>();
	if (!Settings->bGodfreyAcePaceIngestByCurveCatchUp)
	{
		return ClampedConfigBudget;
	}

	const AActor* Character = TargetCharacter.Get();
	if (!Character || StreamSampleRate <= 0)
	{
		return ClampedConfigBudget;
	}

	const UACEAudioCurveSourceComponent* AceComp = Character->FindComponentByClass<UACEAudioCurveSourceComponent>();
	if (!AceComp || !AceComp->IsProceduralAudioPlaying())
	{
		return ClampedConfigBudget;
	}

	const float UnmatchedSec = GetUnmatchedAudioSeconds();

	const float TightThresholdSec = Settings->GodfreyAceMaxUnmatchedAudioSeconds;
	const float MediumThresholdSec = FMath::Min(
		Settings->GodfreyAceSoftThrottleMediumUnmatchedSeconds,
		TightThresholdSec - 0.05f);

	if (UnmatchedSec >= TightThresholdSec)
	{
		// Keep at least 1 push/tick so final PCM can still drain (budget 0 deadlocks FinishStream).
		// Pair with 50ms drain delay so ingest stays near real-time instead of flooding A2F.
		return 1;
	}

	if (UnmatchedSec >= MediumThresholdSec)
	{
		return FMath::Max(1, ClampedConfigBudget / 2);
	}

	return ClampedConfigBudget;
}

void UGodfreyPcmStreamSession::RestoreGodfreyAcePlaybackPrimingIfApplied()
{
	if (!bGodfreyAcePrimingApplied)
	{
		return;
	}

	if (AActor* Character = TargetCharacter.Get())
	{
		if (UACEAudioCurveSourceComponent* AceComp = Character->FindComponentByClass<UACEAudioCurveSourceComponent>())
		{
			if (bGodfreySavedAceBufferLength)
			{
				AceComp->BufferLengthInSeconds = GodfreySavedAceBufferLengthInSeconds;
				bGodfreySavedAceBufferLength = false;
				UE_LOG(LogGodfreyPcmStream, Verbose, TEXT("Godfrey ACE priming: restored BufferLengthInSeconds to %.4f"), AceComp->BufferLengthInSeconds);
			}
			if (bGodfreySavedAceMinBlend)
			{
				AceComp->MinBlendShapeSamplesBeforePlay = GodfreySavedAceMinBlendShapeSamplesBeforePlay;
				bGodfreySavedAceMinBlend = false;
				UE_LOG(LogGodfreyPcmStream, Verbose, TEXT("Godfrey ACE priming: restored MinBlendShapeSamplesBeforePlay to %d"), AceComp->MinBlendShapeSamplesBeforePlay);
			}
			if (bGodfreySavedAceMinCurveLead)
			{
				if (bHoldAudibleUntilReleased)
				{
					UE_LOG(LogGodfreyPcmStream, Log,
						TEXT("Godfrey utterance %d: keeping hold-audible gate after priming restore (arrival card)."),
						UtteranceOrdinal);
				}
				else
				{
					AceComp->MinCurveTimestampSecondsBeforePlay = GodfreySavedAceMinCurveTimestampBeforePlay;
					bGodfreySavedAceMinCurveLead = false;
					UE_LOG(LogGodfreyPcmStream, Verbose, TEXT("Godfrey ACE priming: restored MinCurveTimestampSecondsBeforePlay to %.4f"), AceComp->MinCurveTimestampSecondsBeforePlay);
				}
			}
		}
	}

	bGodfreyAcePrimingApplied = false;
}

void UGodfreyPcmStreamSession::LogAudiblePlaybackDiagnostics(const TCHAR* ContextLabel) const
{
	if (!GetDefault<UUnrealPerformerGodfreySettings>()->bGodfreyLogAudiblePlaybackDiagnostics)
	{
		return;
	}

	AActor* Character = TargetCharacter.Get();
	UWorld* World = Character ? Character->GetWorld() : nullptr;
	const Audio::FDeviceId DeviceId = World ? World->GetAudioDevice().GetDeviceID() : Audio::FDeviceId(INDEX_NONE);

	int32 MixerSampleRate = 0;
	int32 MixerOutputChannels = 0;
	FString MixerDeviceName;
	if (World)
	{
		if (const Audio::FMixerDevice* Mixer = FAudioDeviceManager::GetAudioMixerDeviceFromWorldContext(World))
		{
			MixerSampleRate = Mixer->GetDeviceSampleRate();
			MixerOutputChannels = Mixer->GetDeviceOutputChannels();
			MixerDeviceName = Mixer->GetPlatformDeviceInfo().Name;
		}
	}

	UACEAudioCurveSourceComponent* AceComp = Character ? Character->FindComponentByClass<UACEAudioCurveSourceComponent>() : nullptr;
	const float AceVolume = AceComp ? AceComp->Volume : -1.f;
	const bool bAceProceduralPlaying = AceComp ? AceComp->IsProceduralAudioPlaying() : false;
	const float AcePlaybackWallSec = AceComp ? AceComp->GetProceduralPlaybackWallClockSeconds() : -1.f;
	const float AceMaxCurveTs = AceComp ? AceComp->GetMaxReceivedCurveTimestamp() : -1.f;

	const UAudioComponent* ParallelAC = ParallelAudibleAudioComponent;
	const bool bParallelValid = IsValid(ParallelAC);
	const bool bParallelIsPlaying = bParallelValid && ParallelAC->IsPlaying();
	const EAudioComponentPlayState ParallelPlayState = bParallelValid ? ParallelAC->GetPlayState() : EAudioComponentPlayState::Stopped;
	const float ParallelVolMult = bParallelValid ? ParallelAC->VolumeMultiplier : -1.f;
	const bool bParallelUISound = bParallelValid && ParallelAC->bIsUISound;
	const bool bParallelSpatial = bParallelValid && ParallelAC->bAllowSpatialization;
	const bool bParallelAutoDestroy = bParallelValid && ParallelAC->bAutoDestroy;
	const USoundBase* ParallelSound = ParallelAudibleWave;

	int32 ProceduralAvailBytes = 0;
	int32 ProceduralWaveSampleRate = 0;
	int32 ProceduralWaveChannels = 0;
	float ProceduralWaveDuration = -1.f;
	if (ParallelAudibleWave)
	{
		ProceduralAvailBytes = ParallelAudibleWave->GetAvailableAudioByteCount();
		ProceduralWaveSampleRate = ParallelAudibleWave->GetSampleRateForCurrentPlatform();
		ProceduralWaveChannels = ParallelAudibleWave->NumChannels;
		ProceduralWaveDuration = ParallelAudibleWave->Duration;
	}

	const int32 FrameSize = StreamNumChannels * static_cast<int32>(sizeof(int16));
	const int32 EffectiveParallelSampleRate = GetParallelAudibleEffectiveSampleRate();
	const float ParallelQueuedAudioSec = (FrameSize > 0 && EffectiveParallelSampleRate > 0)
		? static_cast<float>(ParallelAudibleQueuedBytes) / (static_cast<float>(EffectiveParallelSampleRate) * static_cast<float>(FrameSize))
		: 0.f;
	const float ProceduralAvailAudioSec = (FrameSize > 0 && ProceduralWaveSampleRate > 0)
		? static_cast<float>(ProceduralAvailBytes) / (static_cast<float>(ProceduralWaveSampleRate) * static_cast<float>(FrameSize))
		: 0.f;

	bool bAudioDeviceMuted = false;
	float PrimaryVolume = -1.f;
	float TransientPrimaryVolume = -1.f;
	const float AppVolumeMultiplier = FApp::GetVolumeMultiplier();
#if WITH_EDITOR
	const bool bEnableGameSound = GetDefault<ULevelEditorPlaySettings>()->EnableGameSound;
#else
	const bool bEnableGameSound = true;
#endif
	if (World)
	{
		if (const FAudioDeviceHandle DeviceHandle = World->GetAudioDevice())
		{
			if (FAudioDevice* AudioDevice = DeviceHandle.GetAudioDevice())
			{
				bAudioDeviceMuted = AudioDevice->IsAudioDeviceMuted();
				PrimaryVolume = AudioDevice->GetPrimaryVolume();
				TransientPrimaryVolume = SafeGetTransientPrimaryVolume(AudioDevice);
			}
		}
	}

	UE_LOG(LogGodfreyPcmStream, Log,
		TEXT("[Godfrey audible diag] Utterance=%d | %s | PIE=%d Finished=%d"),
		UtteranceOrdinal,
		ContextLabel,
		(World && World->IsPlayInEditor()) ? 1 : 0,
		bFinished ? 1 : 0);

	UE_LOG(LogGodfreyPcmStream, Log,
		TEXT("[Godfrey audible diag] %s | Mixer Device='%s' DeviceId=%u MixerSR=%d MixerOutCh=%d UpsampleToMixer=%d StreamSR=%d StreamCh=%d DeviceMuted=%d PrimaryVol=%.3f TransientVol=%.3f AppMult=%.3f EnableGameSound=%d"),
		ContextLabel,
		MixerDeviceName.IsEmpty() ? TEXT("(unknown)") : *MixerDeviceName,
		static_cast<uint32>(DeviceId),
		MixerSampleRate,
		MixerOutputChannels,
		GetDefault<UUnrealPerformerGodfreySettings>()->bGodfreyUpsamplePcmToMixerRate ? 1 : 0,
		StreamSampleRate,
		StreamNumChannels,
		bAudioDeviceMuted ? 1 : 0,
		PrimaryVolume,
		TransientPrimaryVolume,
		AppVolumeMultiplier,
		bEnableGameSound ? 1 : 0);

	if (MixerDeviceName.Contains(TEXT("EPSON"), ESearchCase::IgnoreCase)
		|| MixerDeviceName.Contains(TEXT("NVIDIA High Definition Audio"), ESearchCase::IgnoreCase))
	{
		UE_LOG(LogGodfreyPcmStream, Log,
			TEXT("[Godfrey audible diag] %s | Mixer output '%s'."),
			ContextLabel,
			*MixerDeviceName);
	}

	if (PrimaryVolume <= KINDA_SMALL_NUMBER && !bAudioDeviceMuted)
	{
		if (AppVolumeMultiplier <= KINDA_SMALL_NUMBER)
		{
			UE_LOG(LogGodfreyPcmStream, Warning,
				TEXT("[Godfrey audible diag] %s | AppMult=0 (editor unfocused): click the PIE viewport or set UnfocusedVolumeMultiplier=1 in DefaultEngine.ini."),
				ContextLabel);
		}
		else
		{
			UE_LOG(LogGodfreyPcmStream, Warning,
				TEXT("[Godfrey audible diag] %s | PrimaryVol~0: check PIE viewport volume slider and Editor Preferences → Play → Enable Game Sound."),
				ContextLabel);
		}
	}

	UE_LOG(LogGodfreyPcmStream, Log,
		TEXT("[Godfrey audible diag] %s | ACE Volume=%.3f AceProceduralPlaying=%d AcePlaybackWallSec=%.3f AceMaxCurveTs=%.3f AceMutedForParallel=%d"),
		ContextLabel,
		AceVolume,
		bAceProceduralPlaying ? 1 : 0,
		AcePlaybackWallSec,
		AceMaxCurveTs,
		bAceVolumeMutedForParallelLipSync ? 1 : 0);

	UE_LOG(LogGodfreyPcmStream, Log,
		TEXT("[Godfrey audible diag] %s | Parallel active=%d started=%d AC=%p IsPlaying=%d PlayState=%s VolMult=%.3f UISound=%d Spatial=%d AutoDestroy=%d Sound=%p"),
		ContextLabel,
		bParallelAudibleActive ? 1 : 0,
		bParallelAudiblePlaybackStarted ? 1 : 0,
		ParallelAC,
		bParallelIsPlaying ? 1 : 0,
		AudioComponentPlayStateToString(ParallelPlayState),
		ParallelVolMult,
		bParallelUISound ? 1 : 0,
		bParallelSpatial ? 1 : 0,
		bParallelAutoDestroy ? 1 : 0,
		ParallelSound);

	UE_LOG(LogGodfreyPcmStream, Log,
		TEXT("[Godfrey audible diag] %s | Procedural queuedBytes=%d (~%.2fs) availBytes=%d (~%.2fs) waveSR=%d waveCh=%d waveDur=%.2f queueCalls=%d underflows=%d"),
		ContextLabel,
		ParallelAudibleQueuedBytes,
		ParallelQueuedAudioSec,
		ProceduralAvailBytes,
		ProceduralAvailAudioSec,
		ProceduralWaveSampleRate,
		ProceduralWaveChannels,
		ProceduralWaveDuration,
		ParallelAudibleQueueCallCount,
		ParallelProceduralUnderflowCount);

	if (ParallelAC && bParallelAudiblePlaybackStarted && !bParallelIsPlaying)
	{
		UE_LOG(LogGodfreyPcmStream, Warning,
			TEXT("[Godfrey audible diag] %s | Parallel path reports started but AudioComponent IsPlaying=0 (PlayState=%s)."),
			ContextLabel,
			AudioComponentPlayStateToString(ParallelPlayState));
	}

	if (bParallelAudibleActive && bParallelAudiblePlaybackStarted && ProceduralAvailBytes <= 0 && ParallelAudibleQueuedBytes > 0)
	{
		UE_LOG(LogGodfreyPcmStream, Warning,
			TEXT("[Godfrey audible diag] %s | Procedural FIFO drained (availBytes=0) while game-thread queuedBytes=%d — possible underflow/starvation."),
			ContextLabel,
			ParallelAudibleQueuedBytes);
	}

	if (RollingPcmBytes.Num() > 0)
	{
		int16 RollingPeak = 0;
		float RollingRms = 0.f;
		ComputePcm16PeakRms(RollingPcmBytes, StreamNumChannels, RollingPeak, RollingRms);
		UE_LOG(LogGodfreyPcmStream, Log,
			TEXT("[Godfrey audible diag] %s | RollingPcmBytes=%d streamPeak=%d streamRms=%.1f (post-gain x%.2f, saturated=%lld gated=%lld gate=%d, source SR=%d before upsample)"),
			ContextLabel,
			RollingPcmBytes.Num(),
			static_cast<int32>(RollingPeak),
			RollingRms,
			GetDefault<UUnrealPerformerGodfreySettings>()->GodfreySpeechPcmGain,
			SpeechGainSaturatedSampleCount,
			SpeechSilenceGatedSampleCount,
			GetDefault<UUnrealPerformerGodfreySettings>()->GodfreySpeechSilenceGate,
			StreamSampleRate);
		if (RollingPeak == 0)
		{
			UE_LOG(LogGodfreyPcmStream, Warning,
				TEXT("[Godfrey audible diag] %s | Entire rolling PCM buffer is silent (peak=0) — check brain stream / ElevenLabs output."),
				ContextLabel);
		}
	}
}

void UGodfreyPcmStreamSession::ScheduleAudibleDiagnosticsTimer(UWorld* World)
{
	if (!World)
	{
		return;
	}

	CancelAudibleDiagnosticsTimer();
	AudibleDiagnosticsWorld = World;
	AudibleDiagnosticsTickCount = 0;
	// Always run keep-alive (not only when diagnostics logging is on).
	// EditorEngine sets AppMult=0 every frame when unfocused unless bAllowBackgroundAudio.
	World->GetTimerManager().SetTimer(
		AudibleDiagnosticsTimerHandle,
		FTimerDelegate::CreateUObject(this, &UGodfreyPcmStreamSession::AudibleDiagnosticsTimerTick),
		0.05f,
		true);
}

void UGodfreyPcmStreamSession::CancelAudibleDiagnosticsTimer()
{
	if (UWorld* World = AudibleDiagnosticsWorld.Get())
	{
		World->GetTimerManager().ClearTimer(AudibleDiagnosticsTimerHandle);
	}
	AudibleDiagnosticsWorld.Reset();
	AudibleDiagnosticsTimerHandle.Invalidate();
}

void UGodfreyPcmStreamSession::AudibleDiagnosticsTimerTick()
{
	++AudibleDiagnosticsTickCount;
	if (UWorld* World = AudibleDiagnosticsWorld.Get())
	{
#if WITH_EDITOR
		if (ULevelEditorMiscSettings* Misc = GetMutableDefault<ULevelEditorMiscSettings>())
		{
			Misc->bAllowBackgroundAudio = true;
			Misc->EditorVolumeLevel = 1.f;
		}
#endif
		FApp::SetUnfocusedVolumeMultiplier(1.f);
		FApp::SetVolumeMultiplier(1.f);
		TryRestorePieAudibilityIfSilent(World, TEXT("periodic-tick"));
		if (const FAudioDeviceHandle DeviceHandle = World->GetAudioDevice())
		{
			if (FAudioDevice* AudioDevice = DeviceHandle.GetAudioDevice())
			{
				// GetTransientPrimaryVolume asserts off the audio thread; PrimaryVolume is safe here.
				if (AudioDevice->GetPrimaryVolume() <= KINDA_SMALL_NUMBER)
				{
					AudioDevice->SetTransientPrimaryVolume(1.f);
				}
			}
		}
	}

	if (GetDefault<UUnrealPerformerGodfreySettings>()->bGodfreyLogAudiblePlaybackDiagnostics
		&& (AudibleDiagnosticsTickCount == 1 || (AudibleDiagnosticsTickCount % 40) == 0))
	{
		const FString Label = FString::Printf(TEXT("periodic-tick-%d"), AudibleDiagnosticsTickCount);
		LogAudiblePlaybackDiagnostics(*Label);
	}

	if (bFinished || !bParallelAudiblePlaybackStarted)
	{
		CancelAudibleDiagnosticsTimer();
	}
}

void UGodfreyPcmStreamSession::BindParallelAudibleUnderflowDelegate()
{
	if (!ParallelAudibleWave)
	{
		return;
	}

	ParallelAudibleWave->OnSoundWaveProceduralUnderflow.Unbind();
	ParallelAudibleWave->OnSoundWaveProceduralUnderflow.BindUObject(this, &UGodfreyPcmStreamSession::HandleParallelProceduralUnderflow);
}

void UGodfreyPcmStreamSession::UnbindParallelAudibleUnderflowDelegate()
{
	if (ParallelAudibleWave)
	{
		ParallelAudibleWave->OnSoundWaveProceduralUnderflow.Unbind();
	}
}

void UGodfreyPcmStreamSession::HandleParallelProceduralUnderflow(USoundWaveProcedural* Wave, int32 SamplesRequired)
{
	++ParallelProceduralUnderflowCount;
	if (!GetDefault<UUnrealPerformerGodfreySettings>()->bGodfreyLogAudiblePlaybackDiagnostics)
	{
		return;
	}

	const int32 AvailBytes = Wave ? Wave->GetAvailableAudioByteCount() : 0;
	const int32 FrameSize = StreamNumChannels * static_cast<int32>(sizeof(int16));
	const int32 SamplesRequiredBytes = (FrameSize > 0) ? (SamplesRequired * FrameSize) : 0;
	const bool bLikelyStarvation = AvailBytes < SamplesRequiredBytes;
	if (bLikelyStarvation && ParallelProceduralUnderflowCount <= 10)
	{
		UE_LOG(LogGodfreyPcmStream, Warning,
			TEXT("[Godfrey audible diag] procedural underflow #%d | Utterance=%d SamplesRequired=%d availBytes=%d queuedBytes(game)=%d started=%d likelyStarvation=1"),
			ParallelProceduralUnderflowCount,
			UtteranceOrdinal,
			SamplesRequired,
			AvailBytes,
			ParallelAudibleQueuedBytes,
			bParallelAudiblePlaybackStarted ? 1 : 0);
	}
	else if (bLikelyStarvation && ParallelProceduralUnderflowCount == 11)
	{
		UE_LOG(LogGodfreyPcmStream, Warning,
			TEXT("[Godfrey audible diag] procedural underflow | Utterance=%d — suppressing further starvation warnings (count>10)."),
			UtteranceOrdinal);
	}
	else
	{
		UE_LOG(LogGodfreyPcmStream, Verbose,
			TEXT("[Godfrey audible diag] procedural underflow #%d | Utterance=%d SamplesRequired=%d availBytes=%d queuedBytes(game)=%d started=%d likelyStarvation=0"),
			ParallelProceduralUnderflowCount,
			UtteranceOrdinal,
			SamplesRequired,
			AvailBytes,
			ParallelAudibleQueuedBytes,
			bParallelAudiblePlaybackStarted ? 1 : 0);
	}
}

void UGodfreyPcmStreamSession::PrepareFreshParallelAudibleWave(AActor* Character)
{
	UnbindParallelAudibleUnderflowDelegate();
	ParallelAudibleWave = nullptr;

	if (!Character)
	{
		return;
	}

	EnsureParallelAudibleWave(Character);
}

void UGodfreyPcmStreamSession::AbortParallelAudiblePlaybackForAceResync(bool bRestoreAceVolume)
{
	CancelAudibleDiagnosticsTimer();

	if (ParallelAudibleAudioComponent)
	{
		ParallelAudibleAudioComponent->Stop();
		ParallelAudibleAudioComponent->DestroyComponent();
		ParallelAudibleAudioComponent = nullptr;
	}

	if (ParallelAudibleWave)
	{
		ParallelAudibleWave->ResetAudio();
	}

	ParallelAudibleQueuedBytes = 0;
	bParallelAudiblePlaybackStarted = false;
	ParallelAudibleQueueCallCount = 0;
	ParallelProceduralUnderflowCount = 0;

	if (bRestoreAceVolume && bSavedAceVolumeForParallelMute)
	{
		if (AActor* Character = TargetCharacter.Get())
		{
			if (UACEAudioCurveSourceComponent* AceComp = Character->FindComponentByClass<UACEAudioCurveSourceComponent>())
			{
				AceComp->Volume = SavedAceVolumeBeforeParallelMute;
			}
		}
		bSavedAceVolumeForParallelMute = false;
	}

	bAceVolumeMutedForParallelLipSync = false;
}

void UGodfreyPcmStreamSession::EnsureParallelAudibleWave(AActor* Character)
{
	if (!Character || ParallelAudibleWave)
	{
		return;
	}

	// Match Test_Live_Audio WavUrlSoundLibrary: transient outer, native rate, finite duration set at queue time.
	ParallelAudibleWave = NewObject<USoundWaveProcedural>(GetTransientPackage());
	if (!ParallelAudibleWave)
	{
		UE_LOG(LogGodfreyPcmStream, Warning, TEXT("Godfrey utterance %d: failed to allocate parallel USoundWaveProcedural."), UtteranceOrdinal);
		return;
	}

	ParallelAudibleWave->SetSampleRate(GetParallelAudibleEffectiveSampleRate());
	ParallelAudibleWave->NumChannels = StreamNumChannels;
	ParallelAudibleWave->Duration = 0.f;
	ParallelAudibleWave->bLooping = false;
	ParallelAudibleWave->Volume = 1.f;
	ParallelAudibleWave->SoundGroup = SOUNDGROUP_Default;
	ParallelAudibleWave->VirtualizationMode = EVirtualizationMode::PlayWhenSilent;
	BindParallelAudibleUnderflowDelegate();
}

void UGodfreyPcmStreamSession::UpdateParallelAudibleWaveDuration()
{
	if (!ParallelAudibleWave || ParallelAudibleQueuedBytes <= 0)
	{
		return;
	}

	const int32 EffectiveSampleRate = GetParallelAudibleEffectiveSampleRate();
	const int32 FrameSize = StreamNumChannels * static_cast<int32>(sizeof(int16));
	if (EffectiveSampleRate <= 0 || FrameSize <= 0)
	{
		return;
	}

	const int32 NumFrames = ParallelAudibleQueuedBytes / FrameSize;
	ParallelAudibleWave->Duration = static_cast<float>(NumFrames) / static_cast<float>(EffectiveSampleRate);
	ParallelAudibleWave->TotalSamples = NumFrames * StreamNumChannels;
	ParallelAudibleWave->RawPCMDataSize = ParallelAudibleQueuedBytes;
	if (bFinished)
	{
		ParallelAudibleWave->bLooping = false;
	}
}

void UGodfreyPcmStreamSession::MuteAceVolumeForParallelLipSyncOnly()
{
	AActor* Character = TargetCharacter.Get();
	if (!Character)
	{
		return;
	}

	UACEAudioCurveSourceComponent* AceComp = Character->FindComponentByClass<UACEAudioCurveSourceComponent>();
	if (!AceComp)
	{
		return;
	}

	if (!bSavedAceVolumeForParallelMute)
	{
		SavedAceVolumeBeforeParallelMute = AceComp->Volume;
		bSavedAceVolumeForParallelMute = true;
	}

	// Always re-apply: ACE Play()/OnAnimationStarted can restore Volume after an earlier mute.
	AceComp->Volume = 0.f;
	bAceVolumeMutedForParallelLipSync = true;
}

void UGodfreyPcmStreamSession::QueueParallelAudiblePcm(const TArray<uint8>& PcmBytes)
{
	if (!bParallelAudibleActive || !ParallelAudibleWave || PcmBytes.Num() <= 0)
	{
		return;
	}

	TArray<uint8> AudiblePcm;
	int32 AudibleSampleRate = StreamSampleRate;
	UpsamplePcm16MonoForAudiblePlayback(PcmBytes, StreamSampleRate, AudiblePcm, AudibleSampleRate);
	// Sample rate is fixed in EnsureParallelAudibleWave — do not call SetSampleRate after Play(); that resets procedural playback.

	ParallelAudibleWave->QueueAudio(AudiblePcm.GetData(), AudiblePcm.Num());
	ParallelAudibleQueuedBytes += AudiblePcm.Num();
	if (bFinished)
	{
		UpdateParallelAudibleWaveDuration();
	}
	else if (ParallelAudibleWave)
	{
		ParallelAudibleWave->Duration = INDEFINITELY_LOOPING_DURATION;
	}
	++ParallelAudibleQueueCallCount;

	if (!bLoggedFirstParallelQueue)
	{
		bLoggedFirstParallelQueue = true;
		int16 Peak = 0;
		float Rms = 0.f;
		ComputePcm16PeakRms(AudiblePcm, StreamNumChannels, Peak, Rms);
		UE_LOG(LogGodfreyPcmStream, Log,
			TEXT("Godfrey utterance %d: first parallel PCM queue. ChunkBytes=%d AudibleBytes=%d AudibleSR=%d Peak=%d Rms=%.1f TotalQueued=%d (brain may send ~80ms lead silence first)"),
			UtteranceOrdinal,
			PcmBytes.Num(),
			AudiblePcm.Num(),
			AudibleSampleRate,
			static_cast<int32>(Peak),
			Rms,
			ParallelAudibleQueuedBytes);
		LogAudiblePlaybackDiagnostics(TEXT("first-parallel-queue"));
	}
	else if (!bLoggedFirstNonSilentParallelQueue)
	{
		int16 Peak = 0;
		float Rms = 0.f;
		ComputePcm16PeakRms(AudiblePcm, StreamNumChannels, Peak, Rms);
		if (Peak > 0)
		{
			bLoggedFirstNonSilentParallelQueue = true;
			UE_LOG(LogGodfreyPcmStream, Log,
				TEXT("Godfrey utterance %d: first non-silent parallel PCM queue at call #%d. ChunkBytes=%d Peak=%d Rms=%.1f TotalQueued=%d"),
				UtteranceOrdinal,
				ParallelAudibleQueueCallCount,
				PcmBytes.Num(),
				static_cast<int32>(Peak),
				Rms,
				ParallelAudibleQueuedBytes);
		}
	}
	else if (GetDefault<UUnrealPerformerGodfreySettings>()->bGodfreyLogAudiblePlaybackDiagnostics
		&& (ParallelAudibleQueueCallCount % 100) == 0)
	{
		UE_LOG(LogGodfreyPcmStream, Log,
			TEXT("Godfrey utterance %d: parallel PCM queue milestone #%d TotalQueuedBytes=%d availProcedural=%d"),
			UtteranceOrdinal,
			ParallelAudibleQueueCallCount,
			ParallelAudibleQueuedBytes,
			ParallelAudibleWave->GetAvailableAudioByteCount());
	}
}

void UGodfreyPcmStreamSession::TryStartParallelAudiblePlayback(bool bIgnoreBufferThreshold, bool bAceSyncStart)
{
	if (!bParallelAudibleActive)
	{
		return;
	}

	if (bParallelAudiblePlaybackStarted && !bAceSyncStart)
	{
		return;
	}

	if (bParallelAudiblePlaybackStarted && bAceSyncStart)
	{
		UE_LOG(LogGodfreyPcmStream, Warning,
			TEXT("Godfrey utterance %d: aborting early parallel audible play — resyncing to ACE OnAnimationStarted (%s)."),
			UtteranceOrdinal,
			GGodfreyParallelAudibleLogicStamp);
		AbortParallelAudiblePlaybackForAceResync(true);
	}

	AActor* Character = TargetCharacter.Get();
	UWorld* World = Character ? Character->GetWorld() : nullptr;
	if (!World)
	{
		UE_LOG(LogGodfreyPcmStream, Warning,
			TEXT("Godfrey utterance %d: TryStartParallel aborted — no world."),
			UtteranceOrdinal);
		return;
	}

	PrepareFreshParallelAudibleWave(Character);
	if (!ParallelAudibleWave)
	{
		if (bIgnoreBufferThreshold || bAceSyncStart)
		{
			UE_LOG(LogGodfreyPcmStream, Warning,
				TEXT("Godfrey utterance %d: TryStartParallel skipped — failed to allocate wave."),
				UtteranceOrdinal);
		}
		return;
	}

	const int32 FrameSize = StreamNumChannels * static_cast<int32>(sizeof(int16));
	if (FrameSize <= 0)
	{
		if (bIgnoreBufferThreshold)
		{
			UE_LOG(LogGodfreyPcmStream, Warning,
				TEXT("Godfrey utterance %d: TryStartParallel aborted — invalid FrameSize."),
				UtteranceOrdinal);
		}
		return;
	}

	const int32 EffectiveSampleRate = GetParallelAudibleEffectiveSampleRate();
	if (EffectiveSampleRate <= 0)
	{
		if (bIgnoreBufferThreshold)
		{
			UE_LOG(LogGodfreyPcmStream, Warning,
				TEXT("Godfrey utterance %d: TryStartParallel aborted — invalid EffectiveSampleRate."),
				UtteranceOrdinal);
		}
		return;
	}

	// First play: prime procedural FIFO from RollingPcmBytes (ingest does not QueueAudio until after Play).
	int16 RollingPeak = 0;
	float RollingRms = 0.f;
	ComputePcm16PeakRms(RollingPcmBytes, StreamNumChannels, RollingPeak, RollingRms);
	if (RollingPcmBytes.Num() < FrameSize)
	{
		if (!bIgnoreBufferThreshold)
		{
			return;
		}
	}
	else if (RollingPeak == 0)
	{
		if (!bIgnoreBufferThreshold)
		{
			UE_LOG(LogGodfreyPcmStream, Log,
				TEXT("Godfrey utterance %d: deferring parallel audible Play until non-silent PCM exists in rolling buffer (rollingBytes=%d)."),
				UtteranceOrdinal,
				RollingPcmBytes.Num());
			return;
		}
	}

	ParallelAudibleQueuedBytes = 0;

	int32 PcmStartOffset = 0;
	if (bAceSyncStart)
	{
		if (UACEAudioCurveSourceComponent* AceComp = Character->FindComponentByClass<UACEAudioCurveSourceComponent>())
		{
			const float AcePlaybackSec = AceComp->GetProceduralPlaybackWallClockSeconds();
			if (AcePlaybackSec > KINDA_SMALL_NUMBER && StreamSampleRate > 0)
			{
				const int32 SkipFrames = FMath::Clamp(
					FMath::FloorToInt(AcePlaybackSec * static_cast<float>(StreamSampleRate)),
					0,
					RollingPcmBytes.Num() / FrameSize);
				PcmStartOffset = SkipFrames * FrameSize;
				if (PcmStartOffset > 0)
				{
					UE_LOG(LogGodfreyPcmStream, Log,
						TEXT("Godfrey utterance %d: ACE-sync parallel audible — skipping %.3fs (%d bytes) to match ACE playback clock."),
						UtteranceOrdinal,
						AcePlaybackSec,
						PcmStartOffset);
				}
			}
		}
	}

	const int32 RollingBytesToQueue = RollingPcmBytes.Num() - PcmStartOffset;
	if (RollingBytesToQueue >= FrameSize)
	{
		TArray<uint8> AudiblePcm;
		int32 AudibleSampleRate = StreamSampleRate;
		const TArrayView<const uint8> RollingSlice(RollingPcmBytes.GetData() + PcmStartOffset, RollingBytesToQueue);
		TArray<uint8> RollingSliceCopy(RollingSlice.GetData(), RollingBytesToQueue);
		UpsamplePcm16MonoForAudiblePlayback(RollingSliceCopy, StreamSampleRate, AudiblePcm, AudibleSampleRate);
		if (AudiblePcm.Num() >= FrameSize)
		{
			const int32 NumFrames = AudiblePcm.Num() / FrameSize;
			ParallelAudibleWave->SetSampleRate(AudibleSampleRate);
			ParallelAudibleWave->NumChannels = StreamNumChannels;
			ParallelAudibleWave->bLooping = false;
			ParallelAudibleWave->SoundGroup = SOUNDGROUP_Default;
			ParallelAudibleWave->VirtualizationMode = EVirtualizationMode::PlayWhenSilent;
			if (bFinished)
			{
				ParallelAudibleWave->Duration = static_cast<float>(NumFrames) / static_cast<float>(AudibleSampleRate);
				ParallelAudibleWave->TotalSamples = NumFrames * StreamNumChannels;
				ParallelAudibleWave->RawPCMDataSize = AudiblePcm.Num();
			}
			else
			{
				ParallelAudibleWave->Duration = INDEFINITELY_LOOPING_DURATION;
				ParallelAudibleWave->TotalSamples = 0;
				ParallelAudibleWave->RawPCMDataSize = 0;
			}
			ParallelAudibleWave->QueueAudio(AudiblePcm.GetData(), AudiblePcm.Num());
			ParallelAudibleQueuedBytes = AudiblePcm.Num();
		}
	}

	if (ParallelAudibleQueuedBytes < FrameSize)
	{
		UE_LOG(LogGodfreyPcmStream, Warning,
			TEXT("Godfrey utterance %d: parallel audible Play aborted — no primed PCM (rollingBytes=%d queuedAudibleBytes=%d rollingPeak=%d)."),
			UtteranceOrdinal,
			RollingPcmBytes.Num(),
			ParallelAudibleQueuedBytes,
			static_cast<int32>(RollingPeak));
		return;
	}

	if (!bIgnoreBufferThreshold)
	{
		const float BufferSec = GetDefault<UUnrealPerformerGodfreySettings>()->GodfreyAceBufferLengthSeconds;
		const int32 MinBytes = FMath::Max(FrameSize, FMath::RoundToInt(BufferSec * static_cast<float>(EffectiveSampleRate)) * FrameSize);
		if (ParallelAudibleQueuedBytes < MinBytes)
		{
			return;
		}
	}

	UE_LOG(LogGodfreyPcmStream, Log,
		TEXT("Godfrey utterance %d: priming parallel audible at ACE sync (%s). rollingBytes=%d pcmOffset=%d audibleQueuedBytes=%d rollingPeak=%d waveDuration=%.3fs finished=%d"),
		UtteranceOrdinal,
		GGodfreyParallelAudibleLogicStamp,
		RollingPcmBytes.Num(),
		PcmStartOffset,
		ParallelAudibleQueuedBytes,
		static_cast<int32>(RollingPeak),
		ParallelAudibleWave ? ParallelAudibleWave->Duration : 0.f,
		bFinished ? 1 : 0);

	if (ParallelAudibleAudioComponent)
	{
		ParallelAudibleAudioComponent->Stop();
		ParallelAudibleAudioComponent->DestroyComponent();
		ParallelAudibleAudioComponent = nullptr;
	}

	if (bFinished)
	{
		UnbindParallelAudibleUnderflowDelegate();
	}

	const UUnrealPerformerGodfreySettings* Settings = GetDefault<UUnrealPerformerGodfreySettings>();
	TryRestorePieAudibilityIfSilent(World, TEXT("TryStartParallelAudiblePlayback"));
	bool bSpawnedAudible = false;
	if (Settings->bGodfreyAudibleSpawnAtPlayerLocation)
	{
		FVector PlayLocation = Character ? Character->GetActorLocation() : FVector::ZeroVector;
		if (APlayerController* PC = World->GetFirstPlayerController())
		{
			FRotator PlayRotation;
			PC->GetPlayerViewPoint(PlayLocation, PlayRotation);
		}

		ParallelAudibleAudioComponent = UGameplayStatics::SpawnSoundAtLocation(
			World,
			ParallelAudibleWave,
			PlayLocation,
			FRotator::ZeroRotator,
			1.f,
			1.f,
			0.f,
			nullptr,
			nullptr,
			false);

		if (ParallelAudibleAudioComponent)
		{
			// UI sounds ignore some PIE mute paths; ACE procedural path already uses bUISound=1.
			ParallelAudibleAudioComponent->bIsUISound = true;
			ParallelAudibleAudioComponent->bAllowSpatialization = false;
			ParallelAudibleAudioComponent->SetVolumeMultiplier(1.f);
			bSpawnedAudible = true;
		}
	}

	if (!bSpawnedAudible)
	{
		UGameplayStatics::PlaySound2D(World, ParallelAudibleWave);
		ParallelAudibleAudioComponent = nullptr;
	}

	bParallelAudiblePlaybackStarted = true;

	if (Settings->bGodfreyMuteAceWhenParallelAudibleStarts)
	{
		MuteAceVolumeForParallelLipSyncOnly();
	}

	const float QueuedAudioSec = static_cast<float>(ParallelAudibleQueuedBytes) / (static_cast<float>(EffectiveSampleRate) * static_cast<float>(FrameSize));
	const bool bParallelIsPlaying = ParallelAudibleAudioComponent ? ParallelAudibleAudioComponent->IsPlaying() : false;
	UE_LOG(LogGodfreyPcmStream, Log,
		TEXT("Godfrey utterance %d: audible play (%s). spawnAtPlayer=%d AC=%p IsPlaying=%d QueuedBytes=%d SR=%d ~%.2fs waveDur=%.3fs ACE mute=%d."),
		UtteranceOrdinal,
		GGodfreyParallelAudibleLogicStamp,
		Settings->bGodfreyAudibleSpawnAtPlayerLocation ? 1 : 0,
		ParallelAudibleAudioComponent.Get(),
		bParallelIsPlaying ? 1 : 0,
		ParallelAudibleQueuedBytes,
		EffectiveSampleRate,
		QueuedAudioSec,
		ParallelAudibleWave ? ParallelAudibleWave->Duration : 0.f,
		bAceVolumeMutedForParallelLipSync ? 1 : 0);

	LogAudiblePlaybackDiagnostics(TEXT("parallel-play-started"));
	if (UGodfreyDiagnosticsSubsystem* Diag = UGodfreyDiagnosticsSubsystem::Get(World))
	{
		Diag->MarkStage(SpeechId, EGodfreyUtteranceStage::AudioQueued);
		Diag->MarkStage(SpeechId, EGodfreyUtteranceStage::FirstAudibleSample);
	}
	if (AActor* CharacterForTimer = TargetCharacter.Get())
	{
		ScheduleAudibleDiagnosticsTimer(CharacterForTimer->GetWorld());
	}
}

int32 UGodfreyPcmStreamSession::GetParallelAudibleEffectiveSampleRate() const
{
	const int32 MixerSampleRate = GetDefault<UUnrealPerformerGodfreySettings>()->GodfreyMixerUpsampleSampleRate;
	if (StreamSampleRate <= 0)
	{
		return MixerSampleRate;
	}

	if (GetDefault<UUnrealPerformerGodfreySettings>()->bGodfreyUpsamplePcmToMixerRate
		&& StreamSampleRate < MixerSampleRate
		&& (MixerSampleRate % StreamSampleRate) == 0)
	{
		return MixerSampleRate;
	}

	return StreamSampleRate;
}

void UGodfreyPcmStreamSession::StopParallelAudiblePlayback(bool bRestoreAceVolume)
{
	CancelAudibleDiagnosticsTimer();
	UnbindParallelAudibleUnderflowDelegate();

	if (ParallelAudibleAudioComponent)
	{
		ParallelAudibleAudioComponent->Stop();
		ParallelAudibleAudioComponent->DestroyComponent();
		ParallelAudibleAudioComponent = nullptr;
	}

	if (ParallelAudibleWave)
	{
		ParallelAudibleWave->ResetAudio();
		ParallelAudibleWave = nullptr;
	}
	bParallelAudiblePlaybackStarted = false;
	ParallelAudibleQueuedBytes = 0;

	if (bRestoreAceVolume && bSavedAceVolumeForParallelMute)
	{
		if (AActor* Character = TargetCharacter.Get())
		{
			if (UACEAudioCurveSourceComponent* AceComp = Character->FindComponentByClass<UACEAudioCurveSourceComponent>())
			{
				AceComp->Volume = SavedAceVolumeBeforeParallelMute;
			}
		}
		bSavedAceVolumeForParallelMute = false;
	}

	bAceVolumeMutedForParallelLipSync = false;
	bParallelAudibleActive = false;
}

void UGodfreyPcmStreamSession::UpsamplePcm16MonoForAudiblePlayback(
	const TArray<uint8>& SourcePcm,
	int32 SourceSampleRate,
	TArray<uint8>& OutPcm,
	int32& OutSampleRate) const
{
	OutPcm = SourcePcm;
	OutSampleRate = SourceSampleRate;

	if (!GetDefault<UUnrealPerformerGodfreySettings>()->bGodfreyUpsamplePcmToMixerRate || SourceSampleRate <= 0)
	{
		return;
	}

	const int32 MixerSampleRate = GetDefault<UUnrealPerformerGodfreySettings>()->GodfreyMixerUpsampleSampleRate;
	if (SourceSampleRate >= MixerSampleRate || MixerSampleRate <= 0 || (MixerSampleRate % SourceSampleRate) != 0)
	{
		return;
	}

	const int32 UpsampleFactor = MixerSampleRate / SourceSampleRate;
	const int32 FrameSize = StreamNumChannels * static_cast<int32>(sizeof(int16));
	if (FrameSize <= 0 || SourcePcm.Num() < FrameSize || (SourcePcm.Num() % FrameSize) != 0)
	{
		return;
	}

	const int32 NumFrames = SourcePcm.Num() / FrameSize;
	const int16* Src = reinterpret_cast<const int16*>(SourcePcm.GetData());
	OutPcm.SetNumUninitialized(NumFrames * UpsampleFactor * FrameSize);
	int16* Dst = reinterpret_cast<int16*>(OutPcm.GetData());

	for (int32 FrameIndex = 0; FrameIndex < NumFrames; ++FrameIndex)
	{
		for (int32 ChannelIndex = 0; ChannelIndex < StreamNumChannels; ++ChannelIndex)
		{
			const int16 Sample = Src[FrameIndex * StreamNumChannels + ChannelIndex];
			for (int32 RepeatIndex = 0; RepeatIndex < UpsampleFactor; ++RepeatIndex)
			{
				Dst[(FrameIndex * UpsampleFactor + RepeatIndex) * StreamNumChannels + ChannelIndex] = Sample;
			}
		}
	}

	OutSampleRate = MixerSampleRate;
}

void UGodfreyPcmStreamSession::HandleAceAnimationStarted()
{
	if (!IsActiveAceSessionForCharacter())
	{
		UE_LOG(LogGodfreyPcmStream, Verbose,
			TEXT("Godfrey utterance %d: ignoring stale OnAnimationStarted (superseded by a newer session on the same character)."),
			UtteranceOrdinal);
		return;
	}

	if (bAcePlaybackEndedObserved)
	{
		UE_LOG(LogGodfreyPcmStream, Log,
			TEXT("Godfrey utterance %d: ignoring OnAnimationStarted after playback-complete (ACE trailing hush)."),
			UtteranceOrdinal);
		return;
	}

	const double PlatformNow = FPlatformTime::Seconds();
	if (FirstOnAnimationStartedPlatformSeconds < 0.0)
	{
		FirstOnAnimationStartedPlatformSeconds = PlatformNow;
	}
	UtteranceStartupMetrics.AceOnAnimationStartedPlatformSeconds = PlatformNow;
	UtteranceStartupMetrics.bAceOnAnimationStartedObserved = true;

	if (!bParallelAudiblePlaybackStarted)
	{
		if (bParallelAudibleActive)
		{
			ReportError(FString::Printf(
				TEXT("Godfrey utterance %d: parallel audible was active but never started — dual-clock path is disabled; fix config (bGodfreyUseParallelPcmAudiblePlayback=0)."),
				UtteranceOrdinal));
			return;
		}
		// ACE-only: OnAnimationStarted is the single audible + lipsync clock. No parallel start.
	}

	if (AActor* Character = TargetCharacter.Get())
	{
		if (UACEAudioCurveSourceComponent* AceComp = Character->FindComponentByClass<UACEAudioCurveSourceComponent>())
		{
			if (AceComp->Volume <= KINDA_SMALL_NUMBER)
			{
				ReportError(FString::Printf(
					TEXT("Godfrey utterance %d: OnAnimationStarted but ACE Volume=%.4f — lipsync would run with silent/wrong audio. Aborting."),
					UtteranceOrdinal,
					AceComp->Volume));
				return;
			}
		}
	}

	if (bDeferredAceUnbindActive && bFinished && (DeferredUnbindUtteranceOrdinal == UtteranceOrdinal))
	{
		const double DeltaMs = (PlatformNow - DeferredUnbindFinishStreamPlatformSeconds) * 1000.0;
		UE_LOG(LogGodfreyPcmStream, Log,
			TEXT("Godfrey utterance %d: OnAnimationStarted arrived ~%.0f ms after FinishStream (ACE playback/sync trailing HTTP ingest + EndAudioSamples)."),
			UtteranceOrdinal,
			DeltaMs);
		if (GetDefault<UUnrealPerformerGodfreySettings>()->bLogGodfreyAceStartupCompletionSummary)
		{
			LogGodfreyAceStartupCompletionSummary(PlatformNow);
		}
	}

	double WorldNow = -1.0;
	if (const AActor* Character = TargetCharacter.Get())
	{
		if (const UWorld* World = Character->GetWorld())
		{
			WorldNow = World->GetTimeSeconds();
		}
	}

	UE_LOG(LogGodfreyPcmStream, Log,
		TEXT("ACE playback/sync: OnAnimationStarted (internal AudioComponent path). WorldTime=%.6f PlatformTime=%.6f"),
		WorldNow,
		PlatformNow);

	UE_LOG(LogGodfreyPcmStream, Log,
		TEXT("Godfrey utterance %d: OnAnimationStarted — ACE reached STARTED->IN_PROGRESS; OnPlaybackStarted/OnLipSyncStarted will broadcast (audible clock active)."),
		UtteranceOrdinal);

	if (FirstChunkPlatformSeconds >= 0.0)
	{
		UE_LOG(LogGodfreyPcmStream, Log,
			TEXT("ACE timing delta from first PCM chunk: PlatformDelta=%.6fs (chunk at %.6f, animation at %.6f)"),
			PlatformNow - FirstChunkPlatformSeconds,
			FirstChunkPlatformSeconds,
			PlatformNow);
	}

	OnPlaybackStarted.Broadcast();
	OnLipSyncStarted.Broadcast();

	if (UGodfreyDiagnosticsSubsystem* Diag = UGodfreyDiagnosticsSubsystem::Get(TargetCharacter.Get()))
	{
		Diag->MarkStage(SpeechId, EGodfreyUtteranceStage::AudioPlaybackStarted);
		Diag->MarkStage(SpeechId, EGodfreyUtteranceStage::FirstAudibleSample);
	}

	LogAudiblePlaybackDiagnostics(TEXT("OnAnimationStarted"));

	UE_LOG(LogGodfreyPcmStream, Log,
		TEXT("Facial animation pipeline active in sync with ACE audio clock (OnPlaybackStarted + OnLipSyncStarted broadcast)."));

	// Arm audio-end poll immediately so EndSpeaking is not gated on late FinishStream.
	if (AActor* CharacterForWatchdog = TargetCharacter.Get())
	{
		if (UWorld* World = CharacterForWatchdog->GetWorld())
		{
			EnsureAudioEndWatchdogScheduled(World);
		}
	}
}

void UGodfreyPcmStreamSession::CompleteAcePlaybackEnded(const TCHAR* Reason)
{
	if (!IsActiveAceSessionForCharacter())
	{
		UE_LOG(LogGodfreyPcmStream, Verbose,
			TEXT("Godfrey utterance %d: ignoring stale playback-end (%s) — superseded by a newer session on the same character."),
			UtteranceOrdinal,
			Reason ? Reason : TEXT("unknown"));
		return;
	}

	if (bAcePlaybackEndedObserved)
	{
		return;
	}

	const bool bHardTimeout = Reason && FString(Reason).Contains(TEXT("hard-timeout"), ESearchCase::IgnoreCase);
	if (!bHardTimeout)
	{
		if (const AActor* CharacterForTail = TargetCharacter.Get())
		{
			if (const UACEAudioCurveSourceComponent* AceComp = CharacterForTail->FindComponentByClass<UACEAudioCurveSourceComponent>())
			{
				const float Wall = AceComp->GetProceduralPlaybackWallClockSeconds();
				const float EffectiveWall = (Wall >= 0.f) ? Wall : LastPositiveProceduralWallSeconds;
				if (AceComp->IsProceduralAudioPlaying()
					&& GetSentAudioSeconds() > 0.25f
					&& EffectiveWall < (GetSentAudioSeconds() + GodfreyAceAudibleTailSlackSeconds()))
				{
					UE_LOG(LogGodfreyPcmStream, Log,
						TEXT("Godfrey utterance %d: ignoring early playback-end (%s) until sent+buffer (Wall=%.3f Sent=%.3f ProceduralPlaying=1)."),
						UtteranceOrdinal,
						Reason ? Reason : TEXT("unknown"),
						EffectiveWall,
						GetSentAudioSeconds());
					return;
				}
			}
		}
	}

	bAcePlaybackEndedObserved = true;

	const double PlatformNow = FPlatformTime::Seconds();
	double WorldNow = -1.0;
	if (const AActor* Character = TargetCharacter.Get())
	{
		if (const UWorld* World = Character->GetWorld())
		{
			WorldNow = World->GetTimeSeconds();
		}
	}

	UE_LOG(LogGodfreyPcmStream, Log,
		TEXT("Godfrey utterance %d: ACE playback complete (%s). WorldTime=%.6f PlatformTime=%.6f SamplesSentToACE=%lld"),
		UtteranceOrdinal,
		Reason ? Reason : TEXT("unknown"),
		WorldNow,
		PlatformNow,
		TotalSamplesSentToAce);

	// Rest the face only. Do not Volume=0 or Stop() on the normal path — ACE's wall clock
	// leads the speakers, so those calls cut the last sentence (2026-09-01 09:39 mute
	// while ProceduralPlaying=1, then EndSpeaking on the same tick). Abort still Stop()s.
	if (AActor* CharacterForRest = TargetCharacter.Get())
	{
		if (UACEAudioCurveSourceComponent* AceComp = CharacterForRest->FindComponentByClass<UACEAudioCurveSourceComponent>())
		{
			UE_LOG(LogGodfreyPcmStream, Log,
				TEXT("Godfrey utterance %d: rest visemes on playback-complete (Volume=%.3f ProceduralPlaying=%d) — not muting/stopping ACE."),
				UtteranceOrdinal,
				AceComp->Volume,
				AceComp->IsProceduralAudioPlaying() ? 1 : 0);
			AceComp->RequestRestPose();
		}
	}

	if (UGodfreyDiagnosticsSubsystem* Diag = UGodfreyDiagnosticsSubsystem::Get(TargetCharacter.Get()))
	{
		Diag->MarkStage(SpeechId, EGodfreyUtteranceStage::AceComplete);
		Diag->MarkStage(SpeechId, EGodfreyUtteranceStage::SpeechFinished);
	}

	OnPlaybackEnded.Broadcast();

	LogAudiblePlaybackDiagnostics(Reason ? Reason : TEXT("AcePlaybackEnded"));
	StopParallelAudiblePlayback(true);

	if (bDeferredAceUnbindActive && bFinished && (DeferredUnbindUtteranceOrdinal == UtteranceOrdinal))
	{
		CancelDeferredAceUnbind();
		UnbindAceDelegates();
	}
}

bool UGodfreyPcmStreamSession::TryCompleteAcePlaybackFromAudioEndWatchdog()
{
	if (bAcePlaybackEndedObserved || !UtteranceStartupMetrics.bAceOnAnimationStartedObserved)
	{
		return false;
	}

	AActor* Character = TargetCharacter.Get();
	UACEAudioCurveSourceComponent* AceComp = Character ? Character->FindComponentByClass<UACEAudioCurveSourceComponent>() : nullptr;
	if (!AceComp)
	{
		return false;
	}

	const float Wall = AceComp->GetProceduralPlaybackWallClockSeconds();
	const float MaxTs = AceComp->GetMaxReceivedCurveTimestamp();
	const bool bProceduralPlaying = AceComp->IsProceduralAudioPlaying();

	// ACE returns -1 (not a frozen final value) once procedural playback stops, so the raw Wall
	// cannot be compared against the audio duration on the very tick that playback ends. Keep the
	// last real reading so the "procedural stopped" branch below still has a position to judge.
	if (Wall > LastPositiveProceduralWallSeconds)
	{
		LastPositiveProceduralWallSeconds = Wall;
	}
	const float EffectiveWall = (Wall >= 0.f) ? Wall : LastPositiveProceduralWallSeconds;

	const double ExpectedFromSamples = (StreamSampleRate > 0)
		? static_cast<double>(TotalSamplesSentToAce) / static_cast<double>(StreamSampleRate)
		: 0.0;
	const float UnmatchedSec = (ExpectedFromSamples > 0.0)
		? static_cast<float>(ExpectedFromSamples) - MaxTs
		: 0.f;

	const double Now = FPlatformTime::Seconds();
	if (MaxTs > LastObservedMaxCurveTs + 0.01f)
	{
		LastObservedMaxCurveTs = MaxTs;
		MaxCurveTsLastChangePlatformSeconds = Now;
	}
	else if (MaxCurveTsLastChangePlatformSeconds < 0.0 && MaxTs > 0.f)
	{
		LastObservedMaxCurveTs = MaxTs;
		MaxCurveTsLastChangePlatformSeconds = Now;
	}

	constexpr float EpsilonSec = 0.08f;
	constexpr float MaxTsStableSec = 0.35f;
	constexpr float PushQuietSec = 0.50f;
	const float UnmatchedGateSec = GetDefault<UUnrealPerformerGodfreySettings>()->GodfreyAceEndAudioMaxUnmatchedSeconds;

	// Completing playback rest-poses the face. Do not mute/Stop ACE (cuts the delay line).
	// EndAudioSamples is no longer tied to this tick — it runs off-thread at HTTP drain.
	const float TailSlackSec = GodfreyAceAudibleTailSlackSeconds();
	const bool bWallPastSentAudio =
		(Wall >= static_cast<float>(ExpectedFromSamples) + TailSlackSec);
	const bool bWallPastAudibleHush = bWallPastSentAudio;

	const bool bMaxTsStable = (MaxCurveTsLastChangePlatformSeconds > 0.0)
		&& ((Now - MaxCurveTsLastChangePlatformSeconds) >= MaxTsStableSec);
	const bool bPushQuiet = bFinished
		|| (LastSamplePushPlatformSeconds > 0.0 && (Now - LastSamplePushPlatformSeconds) >= PushQuietSec);

	// A 0.5s LLM/TTS gap used to look like end-of-speech (CaughtQuiet + WallPastSamples) while
	// HTTP was still open. Marcia 2026-08-17 "tell me something you wouldn't know": watchdog
	// fired at 1.04s sent / Finished=0, remaining PCM was discarded, then R10 stole the turn.
	if (!bFinished)
	{
		TryBroadcastIngestStallIfPlaybackExhausted(
			Now,
			Wall,
			EffectiveWall,
			ExpectedFromSamples,
			bProceduralPlaying,
			MaxTs,
			bMaxTsStable);
		return false;
	}

	// HTTP is drained: rescan the full gated buffer so LastVoice is not stuck on an early
	// loud window (per-chunk floor used to climb with peak and ignore the calmer ending).
	const int32 SilenceGate = GetDefault<UUnrealPerformerGodfreySettings>()->GodfreySpeechSilenceGate;
	LastVoiceRmsFloorUsed = GodfreyLastVoiceRmsFloor(SpeechPeakAbsSentToAce, SilenceGate);
	if (StreamSampleRate > 0 && RollingPcmBytes.Num() > 0)
	{
		const int32 LastVoiceIdx = FindLastVoicedWindowEndIndex(
			RollingPcmBytes,
			StreamSampleRate,
			LastVoiceRmsFloorUsed);
		if (LastVoiceIdx >= 0)
		{
			LastNonSilentSamplesSentToAce = LastVoiceIdx + 1;
		}
	}

	const double LastVoiceSec = (StreamSampleRate > 0)
		? static_cast<double>(LastNonSilentSamplesSentToAce) / static_cast<double>(StreamSampleRate)
		: 0.0;
	const double LastGateSec = (StreamSampleRate > 0)
		? static_cast<double>(LastGate750SamplesSentToAce) / static_cast<double>(StreamSampleRate)
		: 0.0;

	// Rest when the last voiced sample has drained to the speakers. Wall leads ACE output
	// by BufferLength+DAC (~0.43s). Rest at LastVoice+0.10s closed the mouth while that
	// delay line — and any quieter last syllables the old 16% peak floor had skipped —
	// was still audible (2026-09-01 21:09 every utt; utt-14 rest 2.7s early).
	// Do not wait for Expected+TailSlack (sent hush): that left A2F chewing after the last
	// word ("been to sea yourself?" rest at 9.15, LastVoice=8.21).
	const float RestAfterLastVoiceSec = FMath::Max(0.10f, TailSlackSec);
	const bool bHeardLastVoice =
		(LastVoiceSec > 0.35)
		&& (Wall >= static_cast<float>(LastVoiceSec));
	const bool bPastLastVoiceForRest =
		(LastVoiceSec > 0.35)
		&& (Wall >= static_cast<float>(LastVoiceSec + RestAfterLastVoiceSec));

	if (bPastLastVoiceForRest)
	{
		AceComp->RequestRestPose();
		if (!bLoggedAceRestPoseThisUtterance)
		{
			bLoggedAceRestPoseThisUtterance = true;
			UE_LOG(LogGodfreyPcmStream, Log,
				TEXT("Godfrey utterance %d: rest visemes at last voice (Wall=%.3f LastVoice=%.3f LastVoiceGate=%.3f RestAfter=%.3f ExpectedSamples=%.3f Peak=%d RmsFloor=%d) — not waiting for sent hush."),
				UtteranceOrdinal,
				Wall,
				LastVoiceSec,
				LastGateSec,
				RestAfterLastVoiceSec,
				ExpectedFromSamples,
				SpeechPeakAbsSentToAce,
				LastVoiceRmsFloorUsed);
		}
	}

	// Curves still lagging sent PCM: MaxTs can plateau mid-utterance — do NOT treat Wall≈MaxTs as end.
	const bool bCurvesCaughtUp = UnmatchedSec <= UnmatchedGateSec;
	const bool bPlayedAllSentSamples = (ExpectedFromSamples <= 0.2) || bWallPastAudibleHush;

	const bool bCaughtMaxCurve = bCurvesCaughtUp
		&& bPlayedAllSentSamples
		&& (MaxTs > 0.2f)
		&& bMaxTsStable
		&& (Wall >= (MaxTs - EpsilonSec));

	const bool bMaxTsAgreesWithSamples =
		(MaxTs <= 0.2f) || (MaxTs <= static_cast<float>(ExpectedFromSamples * 1.15) + 0.25f);
	const bool bCaughtSamples =
		bMaxTsAgreesWithSamples
		&& (ExpectedFromSamples > 0.2)
		&& bWallPastAudibleHush;

	const bool bWallPastAllSentSamples =
		bPushQuiet
		&& (ExpectedFromSamples > 0.5)
		&& bWallPastAudibleHush;

	// Audio already silent: do not wait for leftover A2F curves (EndAudio flush / unmatched
	// backlog) — those keep driving lip-sync after the voice has stopped.
	const bool bProceduralStoppedNearEnd =
		!bProceduralPlaying
		&& (EffectiveWall > 0.35f)
		&& bPushQuiet
		&& (bWallPastSentAudio
			|| ((ExpectedFromSamples > 0.5)
				&& (EffectiveWall >= static_cast<float>(ExpectedFromSamples * 0.92f))));

	if (!bCaughtMaxCurve && !bCaughtSamples && !bWallPastAllSentSamples
		&& !bProceduralStoppedNearEnd)
	{
		return false;
	}

	UE_LOG(LogGodfreyPcmStream, Warning,
		TEXT("Godfrey utterance %d: OnAnimationEnded missing — ACE audio-end watchdog firing (Wall=%.3f EffWall=%.3f MaxCurveTs=%.3f ExpectedSamples=%.3f LastVoice=%.3f Unmatched=%.3f ProceduralPlaying=%d Finished=%d CaughtMax=%d CaughtSamples=%d CaughtQuiet=0 WallPastSamples=%d ProceduralNearEnd=%d HeardLastVoice=%d)."),
		UtteranceOrdinal,
		Wall,
		EffectiveWall,
		MaxTs,
		ExpectedFromSamples,
		LastVoiceSec,
		UnmatchedSec,
		bProceduralPlaying ? 1 : 0,
		bFinished ? 1 : 0,
		bCaughtMaxCurve ? 1 : 0,
		bCaughtSamples ? 1 : 0,
		bWallPastAllSentSamples ? 1 : 0,
		bProceduralStoppedNearEnd ? 1 : 0,
		bHeardLastVoice ? 1 : 0);

	CompleteAcePlaybackEnded(TEXT("ACE audio-end watchdog (OnAnimationEnded missing)"));
	return true;
}

void UGodfreyPcmStreamSession::TryBroadcastIngestStallIfPlaybackExhausted(
	const double Now,
	const float Wall,
	const float EffectiveWall,
	const double ExpectedFromSamples,
	const bool bProceduralPlaying,
	const float MaxTs,
	const bool bMaxTsStable)
{
	(void)bMaxTsStable;
	if (bBroadcastIngestStallThisUtterance || bAcePlaybackEndedObserved || bHttpReceiveCompleted)
	{
		return;
	}

	const float StallTimeoutSec = GetDefault<UUnrealPerformerGodfreySettings>()->GodfreyAceIngestStallTimeoutSeconds;
	if (StallTimeoutSec <= 0.f
		|| LastSamplePushPlatformSeconds <= 0.0
		|| ExpectedFromSamples <= 0.5)
	{
		return;
	}

	if ((Now - LastSamplePushPlatformSeconds) < StallTimeoutSec)
	{
		return;
	}
	if (LastHttpBodyProgressPlatformSeconds > 0.0
		&& (Now - LastHttpBodyProgressPlatformSeconds) < StallTimeoutSec)
	{
		return;
	}

	const float TailSlackSec = GodfreyAceAudibleTailSlackSeconds();
	const bool bWallPastSentAudio =
		(Wall >= static_cast<float>(ExpectedFromSamples) + TailSlackSec);
	const bool bProceduralStoppedNearSent =
		!bProceduralPlaying
		&& (EffectiveWall > 0.35f)
		&& (EffectiveWall >= static_cast<float>(ExpectedFromSamples * 0.92f));

	// Do not treat "playhead caught A2F's withheld curve tail" as end of speech — that is ~0.9s
	// early and used to mute/hitch the closing words (Welcome + Georgette story + last reply).
	if (!bWallPastSentAudio && !bProceduralStoppedNearSent)
	{
		return;
	}

	bBroadcastIngestStallThisUtterance = true;

	UE_LOG(LogGodfreyPcmStream, Warning,
		TEXT("Godfrey utterance %d: ingest stall while HTTP still open (quiet=%.1fs Wall=%.3f EffWall=%.3f Expected=%.3f MaxCurveTs=%.3f ProceduralPlaying=%d) — requesting FinishStream."),
		UtteranceOrdinal,
		Now - LastSamplePushPlatformSeconds,
		Wall,
		EffectiveWall,
		ExpectedFromSamples,
		MaxTs,
		bProceduralPlaying ? 1 : 0);

	OnIngestStallWhileAudioCaughtUp.Broadcast();
}

void UGodfreyPcmStreamSession::HandleAceAnimationEnded()
{
	CompleteAcePlaybackEnded(TEXT("OnAnimationEnded"));
}

bool UGodfreyPcmStreamSession::StartStream(UObject* WorldContextObject, AActor* CharacterForAce, FName ProviderName, int32 SampleRate, int32 NumChannels)
{
	if (bStreamStarted && !bFinished)
	{
		ReportError(TEXT("StartStream called while stream is already active."));
		return false;
	}

	if (SampleRate <= 0)
	{
		ReportError(FString::Printf(TEXT("Invalid sample rate: %d"), SampleRate));
		return false;
	}
	if (NumChannels <= 0 || NumChannels > 2)
	{
		ReportError(FString::Printf(TEXT("Unsupported channel count: %d. Only mono/stereo supported."), NumChannels));
		return false;
	}

	if (!CharacterForAce)
	{
		ReportError(TEXT("StartStream: CharacterForAce is required for ACE-only playback."));
		return false;
	}

	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	if (!World)
	{
		ReportError(TEXT("Invalid world context in StartStream."));
		return false;
	}

	UACEAudioCurveSourceComponent* AceComp = CharacterForAce->FindComponentByClass<UACEAudioCurveSourceComponent>();
	if (!AceComp)
	{
		ReportError(FString::Printf(TEXT("StartStream: %s has no UACEAudioCurveSourceComponent (required for ACE audio + curves)."), *CharacterForAce->GetName()));
		return false;
	}

	if (AceComp->Volume <= KINDA_SMALL_NUMBER)
	{
		UE_LOG(LogGodfreyPcmStream, Error,
			TEXT("StartStream: ACE Volume was %.4f on %s — restoring to 1.0 for ACE-only single clock (likely leftover mute from deprecated parallel path)."),
			AceComp->Volume,
			*CharacterForAce->GetName());
		AceComp->Volume = 1.f;
	}

	CancelDeferredAceUnbind();
	UnbindAceDelegates();
	StopParallelAudiblePlayback(false);

	TargetCharacter = CharacterForAce;
	AceProviderName = ProviderName;
	StreamSampleRate = SampleRate;
	StreamNumChannels = NumChannels;
	TotalSamplesSentToAce = 0;
	LastSamplePushPlatformSeconds = -1.0;
	LastHttpBodyProgressPlatformSeconds = -1.0;
	bHttpReceiveCompleted = false;
	LastObservedMaxCurveTs = -1.f;
	MaxCurveTsLastChangePlatformSeconds = -1.0;
	SpeechGainSaturatedSampleCount = 0;
	SpeechSilenceGatedSampleCount = 0;
	LastNonSilentSamplesSentToAce = 0;
	LastGate750SamplesSentToAce = 0;
	SpeechPeakAbsSentToAce = 0;
	LastVoiceRmsFloorUsed = 0;
	bLoggedSpeechGainThisUtterance = false;
	bLoggedAceFaceParamsThisUtterance = false;
	bLoggedAceRestPoseThisUtterance = false;
	bBroadcastIngestStallThisUtterance = false;
	AceFaceParameters = nullptr;
	RollingPcmBytes.Reset();
	bLoggedFirstPcmChunk = false;
	FirstChunkWorldTimeSeconds = -1.0;
	FirstChunkPlatformSeconds = -1.0;
	UtteranceOrdinal = ++GGodfreyUtteranceCounter;
	StreamStartPlatformSeconds = FPlatformTime::Seconds();
	FirstHttpBodyBytesPlatformSeconds = -1.0;
	FirstAnimateSubchunkPlatformSeconds = -1.0;
	FirstOnAnimationStartedPlatformSeconds = -1.0;
	bStreamStarted = true;
	bFinished = false;
	bAceEndAudioSamplesDispatched = false;
	SpeechId.Reset();
	if (UGodfreyDiagnosticsSubsystem* Diag = UGodfreyDiagnosticsSubsystem::Get(World))
	{
		SpeechId = Diag->BeginUtterance(UtteranceOrdinal, BrainRequestId);
	}
	else
	{
		SpeechId = BrainRequestId.IsEmpty()
			? FString::Printf(TEXT("utt-%d"), UtteranceOrdinal)
			: FString::Printf(TEXT("utt-%d-%s"), UtteranceOrdinal, *BrainRequestId.Left(8));
	}
	bGodfreyAcePrimingApplied = false;
	bGodfreySavedAceBufferLength = false;
	bGodfreySavedAceMinBlend = false;
	bGodfreySavedAceMinCurveLead = false;
	bGodfreyAceBufferLengthOverriddenThisUtterance = false;
	bGodfreyAceMinBlendOverriddenThisUtterance = false;
	bGodfreyAceMinCurveLeadOverriddenThisUtterance = false;
	bHoldAudibleUntilReleased = false;
	UtteranceStartupMetrics = FGodfreyAceUtteranceStartupMetrics();
	UtteranceStartupMetrics.UtteranceOrdinal = UtteranceOrdinal;
	UtteranceStartupMetrics.UtteranceT0PlatformSeconds = StreamStartPlatformSeconds;
	bAcePlaybackEndedObserved = false;
	LastPositiveProceduralWallSeconds = -1.f;
	bParallelAudiblePlaybackStarted = false;
	ParallelAudibleQueuedBytes = 0;
	ParallelAudibleQueueCallCount = 0;
	ParallelProceduralUnderflowCount = 0;
	AudibleDiagnosticsTickCount = 0;
	bLoggedFirstParallelQueue = false;
	bLoggedFirstNonSilentParallelQueue = false;
	bParallelAudibleActive = false;
	bAceVolumeMutedForParallelLipSync = false;
	bSavedAceVolumeForParallelMute = false;

	ApplyGodfreyAcePlaybackPriming(AceComp);

	const bool bUseParallelAudible = GetDefault<UUnrealPerformerGodfreySettings>()->bGodfreyUseParallelPcmAudiblePlayback;
	if (bUseParallelAudible)
	{
		UE_LOG(LogGodfreyPcmStream, Error,
			TEXT("Godfrey utterance %d: bGodfreyUseParallelPcmAudiblePlayback=1 is unsupported for exhibition (dual clock drifts lipsync from heard audio). Forcing ACE-only."),
			UtteranceOrdinal);
		// Hard reject dual-clock: do not arm parallel.
		bParallelAudibleActive = false;
	}
	else
	{
		UE_LOG(LogGodfreyPcmStream, Log,
			TEXT("Godfrey utterance %d: ACE-only single clock (heard audio + lipsync from ACE). Parallel path off."),
			UtteranceOrdinal);
	}

	const float AceChunkMsCfg = GetDefault<UUnrealPerformerGodfreySettings>()->AceMaxPcmPushChunkDurationMs;
	const int32 AceSubchunkBytes = ComputeMaxPcmBytesPerAceSubChunk(StreamSampleRate, StreamNumChannels, AceChunkMsCfg);
	const int32 FrameSizeLog = StreamNumChannels * static_cast<int32>(sizeof(int16));
	const int32 SubchunkFrames = (FrameSizeLog > 0) ? (AceSubchunkBytes / FrameSizeLog) : 0;
	const float NominalSubchunkAudioMs = (StreamSampleRate > 0 && SubchunkFrames > 0)
		? (1000.f * static_cast<float>(SubchunkFrames) / static_cast<float>(StreamSampleRate))
		: 0.f;
	UE_LOG(LogGodfreyPcmStream, Log,
		TEXT("Godfrey utterance %d: ACE ingest subchunk — AceMaxPcmPushChunkDurationMs=%.1f (config) -> maxBytes=%d frames=%d nominalAudio=%.2f ms per AnimateFromAudioSamples (HTTP drain uses same cap)."),
		UtteranceOrdinal,
		AceChunkMsCfg,
		AceSubchunkBytes,
		SubchunkFrames,
		NominalSubchunkAudioMs);

	AceComp->OnAnimationStarted.AddDynamic(this, &UGodfreyPcmStreamSession::HandleAceAnimationStarted);
	bBoundAceAnimationStarted = true;
	AceComp->OnAnimationEnded.AddDynamic(this, &UGodfreyPcmStreamSession::HandleAceAnimationEnded);
	bBoundAceAnimationEnded = true;

	RegisterAsActiveAceSessionForCharacter();

	UE_LOG(LogGodfreyPcmStream, Log,
		TEXT("Godfrey utterance %d: fresh session — ACE OnAnimationStarted/OnAnimationEnded bound; audible=%s Character=%s"),
		UtteranceOrdinal,
		bParallelAudibleActive ? TEXT("WavUrl PlaySound2D after OnAnimationStarted") : TEXT("ACE internal AudioComponent after Play()"),
		*CharacterForAce->GetName());

	UE_LOG(LogGodfreyPcmStream, Log,
		TEXT("Stream started. UtteranceOrdinal=%d SampleRate=%d Channels=%d Provider=%s Character=%s ACE_BufferLengthSec=%.4f ACE_Volume=%.3f ParallelAudible=%d StreamT0=%.6f ClientReqT0=%.6f"),
		UtteranceOrdinal,
		StreamSampleRate,
		StreamNumChannels,
		*AceProviderName.ToString(),
		*CharacterForAce->GetName(),
		AceComp->BufferLengthInSeconds,
		AceComp->Volume,
		bParallelAudibleActive ? 1 : 0,
		StreamStartPlatformSeconds,
		ClientRequestT0PlatformSeconds);

	return true;
}

bool UGodfreyPcmStreamSession::WarmupAcePipeline(AActor* CharacterForAce, FName ProviderName, int32 SampleRate, float SilenceDurationSeconds)
{
	if (!CharacterForAce)
	{
		return false;
	}
	UACEAudioCurveSourceComponent* AceComp = CharacterForAce->FindComponentByClass<UACEAudioCurveSourceComponent>();
	if (!AceComp)
	{
		UE_LOG(LogGodfreyPcmStream, Warning, TEXT("WarmupAcePipeline: no UACEAudioCurveSourceComponent on %s"), *CharacterForAce->GetName());
		return false;
	}
	if (SampleRate <= 0)
	{
		return false;
	}

	const int32 NumMonoSamples = FMath::Max(16, FMath::RoundToInt(SilenceDurationSeconds * static_cast<float>(SampleRate)));
	TArray<int16> Silence;
	Silence.SetNumZeroed(NumMonoSamples);

	const double Wall0 = FPlatformTime::Seconds();
	const bool bAnimOk = FACERuntimeModule::Get().AnimateFromAudioSamples(
		AceComp,
		MakeArrayView(Silence.GetData(), Silence.Num()),
		1,
		SampleRate,
		false,
		TOptional<FAudio2FaceEmotion>(),
		nullptr,
		ProviderName);
	const double Wall1 = FPlatformTime::Seconds();

	const bool bEndOk = FACERuntimeModule::Get().EndAudioSamples(AceComp);
	const double Wall2 = FPlatformTime::Seconds();

	UE_LOG(LogGodfreyPcmStream, Log,
		TEXT("ACE warmup: silenceSamples=%d provider=%s AnimateWall=%.3fms EndAudioWall=%.3fms TotalWall=%.3fms AnimateOk=%d EndOk=%d (enable ace.PipelineTimingLog 1 for A2XSession detail)"),
		NumMonoSamples,
		*ProviderName.ToString(),
		(Wall1 - Wall0) * 1000.0,
		(Wall2 - Wall1) * 1000.0,
		(Wall2 - Wall0) * 1000.0,
		bAnimOk ? 1 : 0,
		bEndOk ? 1 : 0);

	return bAnimOk && bEndOk;
}

bool UGodfreyPcmStreamSession::ValidateFormat(const TArray<uint8>& PcmBytes, FString& OutError) const
{
	OutError.Reset();
	if (!bStreamStarted || bFinished)
	{
		OutError = TEXT("Stream not active. Call StartStream first.");
		return false;
	}
	if (PcmBytes.Num() <= 0)
	{
		OutError = TEXT("PCM chunk is empty.");
		return false;
	}

	const int32 FrameSize = StreamNumChannels * static_cast<int32>(sizeof(int16));
	if (FrameSize <= 0 || (PcmBytes.Num() % FrameSize) != 0)
	{
		OutError = FString::Printf(TEXT("PCM chunk alignment invalid. Bytes=%d FrameSize=%d"), PcmBytes.Num(), FrameSize);
		return false;
	}
	return true;
}

int64 UGodfreyPcmStreamSession::ApplySpeechGainToPcm16(TArray<uint8>& PcmBytes, const float Gain)
{
	const int32 SampleCount = PcmBytes.Num() / static_cast<int32>(sizeof(int16));
	if (SampleCount <= 0)
	{
		return 0;
	}

	constexpr float Ceiling = 32767.f;
	// Above the knee the gained signal is compressed into the remaining headroom instead of clipping flat.
	constexpr float Knee = 0.85f * Ceiling;
	constexpr float KneeRange = Ceiling - Knee;

	int64 SaturatedSamples = 0;
	int16* Samples = reinterpret_cast<int16*>(PcmBytes.GetData());
	for (int32 Index = 0; Index < SampleCount; ++Index)
	{
		float Value = static_cast<float>(Samples[Index]) * Gain;
		float Magnitude = FMath::Abs(Value);
		if (Magnitude > Knee)
		{
			const float Over = Magnitude - Knee;
			Magnitude = Knee + KneeRange * (Over / (Over + KneeRange));
			Value = (Value < 0.f) ? -Magnitude : Magnitude;
			++SaturatedSamples;
		}
		Samples[Index] = static_cast<int16>(FMath::Clamp(FMath::RoundToFloat(Value), -32768.f, Ceiling));
	}

	return SaturatedSamples;
}

int64 UGodfreyPcmStreamSession::ApplySpeechSilenceGateToPcm16(TArray<uint8>& PcmBytes, const int32 GateAbs)
{
	if (GateAbs <= 0)
	{
		return 0;
	}

	const int32 SampleCount = PcmBytes.Num() / static_cast<int32>(sizeof(int16));
	if (SampleCount <= 0)
	{
		return 0;
	}

	int64 GatedSamples = 0;
	int16* Samples = reinterpret_cast<int16*>(PcmBytes.GetData());
	for (int32 Index = 0; Index < SampleCount; ++Index)
	{
		if (FMath::Abs(static_cast<int32>(Samples[Index])) < GateAbs)
		{
			Samples[Index] = 0;
			++GatedSamples;
		}
	}
	return GatedSamples;
}

int32 UGodfreyPcmStreamSession::FindLastNonSilentSampleIndex(const TArray<uint8>& PcmBytes, const int32 GateAbs)
{
	const int32 SampleCount = PcmBytes.Num() / static_cast<int32>(sizeof(int16));
	if (SampleCount <= 0)
	{
		return -1;
	}

	const int32 Gate = FMath::Max(1, GateAbs);
	const int16* Samples = reinterpret_cast<const int16*>(PcmBytes.GetData());
	for (int32 Index = SampleCount - 1; Index >= 0; --Index)
	{
		if (FMath::Abs(static_cast<int32>(Samples[Index])) >= Gate)
		{
			return Index;
		}
	}
	return -1;
}

int32 UGodfreyPcmStreamSession::FindLastVoicedWindowEndIndex(const TArray<uint8>& PcmBytes, const int32 SampleRate, const int32 MinRms)
{
	const int32 SampleCount = PcmBytes.Num() / static_cast<int32>(sizeof(int16));
	if (SampleCount <= 0 || SampleRate <= 0)
	{
		return -1;
	}

	const int32 Window = FMath::Clamp(SampleRate / 50, 80, SampleRate / 20);
	const int16* Samples = reinterpret_cast<const int16*>(PcmBytes.GetData());
	const double Floor = static_cast<double>(FMath::Max(1, MinRms));
	const int32 Hop = FMath::Max(1, Window / 2);

	for (int32 End = SampleCount; End >= Window; End -= Hop)
	{
		const int32 Start = End - Window;
		double SumSq = 0.0;
		for (int32 Index = Start; Index < End; ++Index)
		{
			const double Sample = static_cast<double>(Samples[Index]);
			SumSq += Sample * Sample;
		}
		if (FMath::Sqrt(SumSq / static_cast<double>(Window)) >= Floor)
		{
			return End - 1;
		}
	}

	if (SampleCount < Window)
	{
		double SumSq = 0.0;
		for (int32 Index = 0; Index < SampleCount; ++Index)
		{
			const double Sample = static_cast<double>(Samples[Index]);
			SumSq += Sample * Sample;
		}
		if (FMath::Sqrt(SumSq / static_cast<double>(SampleCount)) >= Floor)
		{
			return SampleCount - 1;
		}
	}

	return -1;
}

UAudio2FaceParameters* UGodfreyPcmStreamSession::GetOrCreateAceFaceParameters()
{
	if (AceFaceParameters)
	{
		return AceFaceParameters;
	}

	const UUnrealPerformerGodfreySettings* Settings = GetDefault<UUnrealPerformerGodfreySettings>();
	const float Gain = Settings->GodfreySpeechPcmGain;
	float InputStrength = Settings->GodfreyAceInputStrength;
	if (InputStrength <= 0.f)
	{
		InputStrength = (Gain > 1.01f) ? (1.f / Gain) : 1.f;
	}

	AceFaceParameters = NewObject<UAudio2FaceParameters>(this);
	AceFaceParameters->SetParameter(TEXT("inputStrength"), InputStrength);
	AceFaceParameters->SetParameter(TEXT("lipOpenOffset"), Settings->GodfreyAceLipOpenOffset);

	if (!bLoggedAceFaceParamsThisUtterance)
	{
		bLoggedAceFaceParamsThisUtterance = true;
		UE_LOG(LogGodfreyPcmStream, Log,
			TEXT("Godfrey utterance %d: A2F viseme params inputStrength=%.3f lipOpenOffset=%.3f (gain x%.2f, silenceGate=%d)."),
			UtteranceOrdinal,
			InputStrength,
			Settings->GodfreyAceLipOpenOffset,
			Gain,
			Settings->GodfreySpeechSilenceGate);
	}

	return AceFaceParameters;
}

bool UGodfreyPcmStreamSession::PushPcm16Chunk(const TArray<uint8>& PcmBytes, FString& OutError)
{
	if (!ValidateFormat(PcmBytes, OutError))
	{
		ReportError(OutError);
		return false;
	}

	AActor* Character = TargetCharacter.Get();
	if (!Character)
	{
		OutError = TEXT("PushPcm16Chunk: character lost.");
		ReportError(OutError);
		return false;
	}

	UACEAudioCurveSourceComponent* AceComp = Character->FindComponentByClass<UACEAudioCurveSourceComponent>();
	if (!AceComp)
	{
		OutError = TEXT("PushPcm16Chunk: UACEAudioCurveSourceComponent missing.");
		ReportError(OutError);
		return false;
	}

	if (bAcePlaybackEndedObserved)
	{
		OutError.Reset();
		return true;
	}

	if (!bLoggedFirstPcmChunk)
	{
		bLoggedFirstPcmChunk = true;
		FirstChunkPlatformSeconds = FPlatformTime::Seconds();
		UtteranceStartupMetrics.FirstPcmChunkPlatformSeconds = FirstChunkPlatformSeconds;
		if (const UWorld* W = Character->GetWorld())
		{
			FirstChunkWorldTimeSeconds = W->GetTimeSeconds();
		}
		UE_LOG(LogGodfreyPcmStream, Log,
			TEXT("First PCM chunk received for ACE stream. Bytes=%d SampleRate=%d Channels=%d WorldTime=%.6f PlatformTime=%.6f"),
			PcmBytes.Num(),
			StreamSampleRate,
			StreamNumChannels,
			FirstChunkWorldTimeSeconds,
			FirstChunkPlatformSeconds);
		if (UGodfreyDiagnosticsSubsystem* Diag = UGodfreyDiagnosticsSubsystem::Get(Character))
		{
			Diag->MarkStage(SpeechId, EGodfreyUtteranceStage::AudioReady);
			Diag->MarkStage(SpeechId, EGodfreyUtteranceStage::AceStarted);
		}
	}

	const UUnrealPerformerGodfreySettings* SpeechSettings = GetDefault<UUnrealPerformerGodfreySettings>();
	const float SpeechGain = SpeechSettings->GodfreySpeechPcmGain;
	const int32 SilenceGate = SpeechSettings->GodfreySpeechSilenceGate;
	TArray<uint8> PreparedPcmBytes;
	const bool bApplyGain = !FMath::IsNearlyEqual(SpeechGain, 1.f, 0.01f);
	const bool bApplyGate = SilenceGate > 0;
	if (bApplyGain || bApplyGate)
	{
		PreparedPcmBytes = PcmBytes;
		if (bApplyGain)
		{
			SpeechGainSaturatedSampleCount += ApplySpeechGainToPcm16(PreparedPcmBytes, SpeechGain);
			if (!bLoggedSpeechGainThisUtterance)
			{
				bLoggedSpeechGainThisUtterance = true;
				UE_LOG(LogGodfreyPcmStream, Log,
					TEXT("Godfrey utterance %d: applying speech PCM gain x%.2f before ACE ingest (GodfreySpeechPcmGain)."),
					UtteranceOrdinal,
					SpeechGain);
			}
		}
		if (bApplyGate)
		{
			SpeechSilenceGatedSampleCount += ApplySpeechSilenceGateToPcm16(PreparedPcmBytes, SilenceGate);
		}
	}
	const TArray<uint8>& PcmForAce = (bApplyGain || bApplyGate) ? PreparedPcmBytes : PcmBytes;
	const int64 SamplesBeforeChunk = TotalSamplesSentToAce;

	const float ChunkMs = GetDefault<UUnrealPerformerGodfreySettings>()->AceMaxPcmPushChunkDurationMs;
	const int32 MaxBytesPerAceCall = ComputeMaxPcmBytesPerAceSubChunk(StreamSampleRate, StreamNumChannels, ChunkMs);
	const int32 FrameSize = StreamNumChannels * static_cast<int32>(sizeof(int16));
	const int32 TotalBytes = PcmForAce.Num();

	for (int32 OffsetBytes = 0; OffsetBytes < TotalBytes; OffsetBytes += MaxBytesPerAceCall)
	{
		const int32 SubBytes = FMath::Min(MaxBytesPerAceCall, TotalBytes - OffsetBytes);
		check((SubBytes % FrameSize) == 0);
		const int16* SubPtr = reinterpret_cast<const int16*>(PcmForAce.GetData() + OffsetBytes);
		const int32 SubInt16Count = SubBytes / static_cast<int32>(sizeof(int16));
		const int32 SubFrames = SubInt16Count / StreamNumChannels;
		const double ChunkDurationSec = static_cast<double>(SubFrames) / static_cast<double>(StreamSampleRate);

		const double WallBeforeAnimate = FPlatformTime::Seconds();
		if (AceIsPipelineTimingLogEnabled())
		{
			const double DeltaSinceFirstPcmSec = (FirstChunkPlatformSeconds >= 0.0) ? (WallBeforeAnimate - FirstChunkPlatformSeconds) : 0.0;
			UE_LOG(LogGodfreyPcmStream, Log,
				TEXT("[ACE pipeline] PushPcm16Chunk -> AnimateFromAudioSamples int16Count=%d frames=%d bytes=%d totalSamplesSentBefore=%lld deltaSinceFirstPcm=%.3fs"),
				SubInt16Count,
				SubFrames,
				SubBytes,
				TotalSamplesSentToAce,
				DeltaSinceFirstPcmSec);
		}

		const bool bSent = FACERuntimeModule::Get().AnimateFromAudioSamples(
			AceComp,
			MakeArrayView(SubPtr, SubInt16Count),
			StreamNumChannels,
			StreamSampleRate,
			false,
			TOptional<FAudio2FaceEmotion>(),
			GetOrCreateAceFaceParameters(),
			AceProviderName);

		const double WallAfterAnimate = FPlatformTime::Seconds();
		if (GetDefault<UUnrealPerformerGodfreySettings>()->bLogPerAnimateChunkWallTime)
		{
			UE_LOG(LogGodfreyPcmStream, Log,
				TEXT("[Godfrey ACE] Utterance=%d Animate subchunk frames=%d samples=%d chunkMs=%.2f wallMs=%.3f ok=%d"),
				UtteranceOrdinal,
				SubFrames,
				SubInt16Count,
				ChunkDurationSec * 1000.0,
				(WallAfterAnimate - WallBeforeAnimate) * 1000.0,
				bSent ? 1 : 0);
		}

		if (AceIsPipelineTimingLogEnabled())
		{
			UE_LOG(LogGodfreyPcmStream, Log,
				TEXT("[ACE pipeline] PushPcm16Chunk AnimateFromAudioSamples returned wall=%.3fms ok=%d"),
				(WallAfterAnimate - WallBeforeAnimate) * 1000.0,
				bSent ? 1 : 0);
		}

		if (!bSent)
		{
			OutError = FString::Printf(TEXT("ACE rejected audio chunk for provider '%s'."), *AceProviderName.ToString());
			ReportError(OutError);
			return false;
		}

		if (FirstAnimateSubchunkPlatformSeconds < 0.0)
		{
			FirstAnimateSubchunkPlatformSeconds = WallBeforeAnimate;
			UtteranceStartupMetrics.FirstAnimateFromAudioCallPlatformSeconds = WallBeforeAnimate;
		}

		TotalSamplesSentToAce += SubFrames;
	}

	{
		const int16* PeakSamples = reinterpret_cast<const int16*>(PcmForAce.GetData());
		const int32 PeakCount = PcmForAce.Num() / static_cast<int32>(sizeof(int16));
		for (int32 Index = 0; Index < PeakCount; ++Index)
		{
			SpeechPeakAbsSentToAce = FMath::Max(SpeechPeakAbsSentToAce, FMath::Abs(static_cast<int32>(PeakSamples[Index])));
		}

		const int32 LastGateInChunk = FindLastNonSilentSampleIndex(PcmForAce, FMath::Max(1, SilenceGate));
		if (LastGateInChunk >= 0)
		{
			LastGate750SamplesSentToAce = SamplesBeforeChunk + LastGateInChunk + 1;
		}

		LastVoiceRmsFloorUsed = GodfreyLastVoiceRmsFloor(SpeechPeakAbsSentToAce, SilenceGate);
		const int32 LastVoiceInChunk = FindLastVoicedWindowEndIndex(
			PcmForAce,
			StreamSampleRate,
			LastVoiceRmsFloorUsed);
		if (LastVoiceInChunk >= 0)
		{
			LastNonSilentSamplesSentToAce = SamplesBeforeChunk + LastVoiceInChunk + 1;
		}
	}

	LastSamplePushPlatformSeconds = FPlatformTime::Seconds();
	RollingPcmBytes.Append(PcmForAce);

	if (bParallelAudibleActive)
	{
		if (bParallelAudiblePlaybackStarted)
		{
			QueueParallelAudiblePcm(PcmForAce);
		}
	}

	UE_LOG(LogGodfreyPcmStream, Verbose, TEXT("PCM sent to ACE. ChunkBytes=%d TotalSamplesToACE=%lld BufferedBytes=%d"),
		PcmForAce.Num(),
		TotalSamplesSentToAce,
		RollingPcmBytes.Num());

	return true;
}

bool UGodfreyPcmStreamSession::FinishStream(FString& OutError)
{
	OutError.Reset();
	if (!bStreamStarted || bFinished)
	{
		OutError = TEXT("FinishStream called without an active stream.");
		ReportError(OutError);
		return false;
	}

	const double FinishPlatformSeconds = FPlatformTime::Seconds();
	const bool bProceduralPlayingAtFinish = [this]()
	{
		if (const AActor* Character = TargetCharacter.Get())
		{
			if (const UACEAudioCurveSourceComponent* AceComp = Character->FindComponentByClass<UACEAudioCurveSourceComponent>())
			{
				return AceComp->IsProceduralAudioPlaying();
			}
		}
		return false;
	}();

	UE_LOG(LogGodfreyPcmStream, Log,
		TEXT("Godfrey utterance %d: FinishStream — dispatching EndAudioSamples off game thread. SamplesSentToACE=%lld BufferedBytes=%d ProceduralPlaying=%d Wall=%.3f Unmatched=%.3fs"),
		UtteranceOrdinal,
		TotalSamplesSentToAce,
		RollingPcmBytes.Num(),
		bProceduralPlayingAtFinish ? 1 : 0,
		GetPlaybackWallSeconds(),
		GetUnmatchedAudioSeconds());

	if (AActor* Character = TargetCharacter.Get())
	{
		if (UACEAudioCurveSourceComponent* AceComp = Character->FindComponentByClass<UACEAudioCurveSourceComponent>())
		{
			const UUnrealPerformerGodfreySettings* Settings = GetDefault<UUnrealPerformerGodfreySettings>();
			if (Settings->bGodfreyAceHoldPlayUntilStreamEnd
				&& !bHoldAudibleUntilReleased
				&& bGodfreyAceMinCurveLeadOverriddenThisUtterance)
			{
				AceComp->MinCurveTimestampSecondsBeforePlay = 0.f;
				UE_LOG(LogGodfreyPcmStream, Log,
					TEXT("Godfrey utterance %d: released hold-play gate (MinCurveTimestampSecondsBeforePlay -> 0) before EndAudioSamples. SamplesSentToACE=%lld"),
					UtteranceOrdinal,
					TotalSamplesSentToAce);
			}

			if (!bAceEndAudioSamplesDispatched)
			{
				bAceEndAudioSamplesDispatched = true;
				const float UnmatchedBeforeEnd = GetUnmatchedAudioSeconds();
				const int32 Ordinal = UtteranceOrdinal;
				TWeakObjectPtr<UACEAudioCurveSourceComponent> WeakAce(AceComp);
				UE_LOG(LogGodfreyPcmStream, Log,
					TEXT("Godfrey utterance %d: ACE EndAudioSamples dispatched off game thread (not playback start). PlatformTime=%.6f SamplesSentToACE=%lld UnmatchedBeforeEnd=%.3fs"),
					UtteranceOrdinal,
					FPlatformTime::Seconds(),
					TotalSamplesSentToAce,
					UnmatchedBeforeEnd);

				Async(EAsyncExecution::ThreadPool, [WeakAce, Ordinal, UnmatchedBeforeEnd]()
				{
					UACEAudioCurveSourceComponent* Ace = WeakAce.Get();
					if (!Ace)
					{
						UE_LOG(LogGodfreyPcmStream, Warning,
							TEXT("Godfrey utterance %d: async EndAudioSamples skipped — ACE component gone."),
							Ordinal);
						return;
					}

					const double EndAudioSamplesPlatformSeconds = FPlatformTime::Seconds();
					const bool bEnded = FACERuntimeModule::Get().EndAudioSamples(Ace);
					const double EndAudioWallMs = (FPlatformTime::Seconds() - EndAudioSamplesPlatformSeconds) * 1000.0;
					UE_LOG(LogGodfreyPcmStream, Log,
						TEXT("Godfrey utterance %d: async EndAudioSamples returned wall=%.1fms ok=%d UnmatchedBefore=%.3fs (off game thread)"),
						Ordinal,
						EndAudioWallMs,
						bEnded ? 1 : 0,
						UnmatchedBeforeEnd);
				});
			}
		}
	}

	LogAudiblePlaybackDiagnostics(TEXT("FinishStream"));

	RestoreGodfreyAcePlaybackPrimingIfApplied();

	UWorld* CharacterWorld = nullptr;
	if (AActor* Character = TargetCharacter.Get())
	{
		CharacterWorld = Character->GetWorld();
	}

	const float DelegateGraceSeconds = GetDefault<UUnrealPerformerGodfreySettings>()->GodfreyAcePostFinishOnAnimationStartedDelegateGraceSeconds;
	bFinished = true;

	// Dual-clock FinishStream audible start removed — ACE OnAnimationStarted is the only play gate.

	if (CharacterWorld)
	{
		ScheduleDeferredAceUnbindAfterFinishStream(CharacterWorld, FinishPlatformSeconds);
	}
	else
	{
		UE_LOG(LogGodfreyPcmStream, Warning,
			TEXT("Godfrey utterance %d: no world at FinishStream — cannot defer ACE delegate unbind; OnPlaybackEnded may not fire."),
			UtteranceOrdinal);
		UnbindAceDelegates();
	}

	UE_LOG(LogGodfreyPcmStream, Log, TEXT("Stream finished. Utterance=%d SamplesSentToACE=%lld BufferedBytes=%d"), UtteranceOrdinal, TotalSamplesSentToAce, RollingPcmBytes.Num());
	LogUtteranceLatencySummaryAtFinishIfEnabled(FinishPlatformSeconds);
	if (FirstOnAnimationStartedPlatformSeconds < 0.0)
	{
		UE_LOG(LogGodfreyPcmStream, Warning,
			TEXT("Godfrey utterance %d: OnAnimationStarted not observed yet at FinishStream snapshot (latency summary shows -1s). ACE may still be promoting playback after EndAudioSamples; ")
				TEXT("if GodfreyAcePostFinishOnAnimationStartedDelegateGraceSeconds > 0, delegate stays bound and a supplemental startup summary may log when ACE fires. Otherwise check BufferLengthInSeconds / MinBlendShapeSamplesBeforePlay and ace.GodfreyStartupTiming=1."),
			UtteranceOrdinal);
	}
	else
	{
		UE_LOG(LogGodfreyPcmStream, Log,
			TEXT("Godfrey utterance %d: ACE OnAnimationStarted observed — audible path active for this utterance."),
			UtteranceOrdinal);
	}
	LogGodfreyAceStartupCompletionSummary(FinishPlatformSeconds);
	OnFinished.Broadcast();
	return true;
}

void UGodfreyPcmStreamSession::StopStream()
{
	const double StopWallSeconds = FPlatformTime::Seconds();
	CancelDeferredAceUnbind();
	CancelAudibleDiagnosticsTimer();
	RestoreGodfreyAcePlaybackPrimingIfApplied();
	StopParallelAudiblePlayback(true);
	if (bStreamStarted && !bAcePlaybackEndedObserved)
	{
		OnPlaybackEnded.Broadcast();
		bAcePlaybackEndedObserved = true;
	}
	UnregisterActiveAceSessionForCharacter();
	UnbindAceDelegates();
	bFinished = true;
	bStreamStarted = false;
	LogGodfreyAceStartupCompletionSummary(StopWallSeconds);
	UE_LOG(LogGodfreyPcmStream, Log, TEXT("Stream stopped (ACE-only; delegates unbound)."));
}

void UGodfreyPcmStreamSession::AbortActiveStreamForCharacter(AActor* Character, const FString& Reason)
{
	if (!IsValid(Character))
	{
		return;
	}

	const TWeakObjectPtr<AActor> Key(Character);
	UGodfreyPcmStreamSession* Session = nullptr;
	if (const TWeakObjectPtr<UGodfreyPcmStreamSession>* Existing = GActiveGodfreyAceSessionByCharacter.Find(Key))
	{
		Session = Existing->Get();
	}

	UACEAudioCurveSourceComponent* AceComp = Character->FindComponentByClass<UACEAudioCurveSourceComponent>();
	const bool bAcePlaying = AceComp && AceComp->IsProceduralAudioPlaying();
	if (!Session && !bAcePlaying)
	{
		return;
	}

	UE_LOG(LogGodfreyPcmStream, Log,
		TEXT("Aborting ACE stream on %s (%s) session=%s playing=%d — Stop then EndAudioSamples (close A2X)."),
		*Character->GetName(),
		*Reason,
		Session ? *FString::Printf(TEXT("utt-%d"), Session->UtteranceOrdinal) : TEXT("none"),
		bAcePlaying ? 1 : 0);

	if (Session)
	{
		Session->StopStream();
	}

	if (!AceComp)
	{
		return;
	}

	const float SavedVolume = AceComp->Volume;
	AceComp->Volume = 0.f;
	AceComp->Stop();

	if (Session && Session->bAceEndAudioSamplesDispatched)
	{
		AceComp->Volume = (SavedVolume > KINDA_SMALL_NUMBER) ? SavedVolume : 1.f;
		return;
	}

	if (Session)
	{
		Session->bAceEndAudioSamplesDispatched = true;
	}

	const double EndAudioT0 = FPlatformTime::Seconds();
	const bool bEnded = FACERuntimeModule::Get().EndAudioSamples(AceComp);
	const double EndAudioMs = (FPlatformTime::Seconds() - EndAudioT0) * 1000.0;
	UE_LOG(LogGodfreyPcmStream, Log,
		TEXT("ACE teardown EndAudioSamples on %s (%s) wall=%.1fms ok=%d (hitch on stop is expected if unmatched was large)."),
		*Character->GetName(),
		*Reason,
		EndAudioMs,
		bEnded ? 1 : 0);

	AceComp->Volume = (SavedVolume > KINDA_SMALL_NUMBER) ? SavedVolume : 1.f;
}

void UGodfreyPcmStreamSession::LogUtteranceLatencySummaryAtFinishIfEnabled(double FinishPlatformSeconds) const
{
	if (!GetDefault<UUnrealPerformerGodfreySettings>()->bLogUtteranceLatencySummaryAtStreamFinish)
	{
		return;
	}

	auto DeltaSec = [](double From, double To) -> double
	{
		if (From < 0.0 || To < 0.0)
		{
			return -1.0;
		}
		return To - From;
	};

	const double DtClientToHttp = DeltaSec(ClientRequestT0PlatformSeconds, FirstHttpBodyBytesPlatformSeconds);
	const double DtClientToFirstPcm = DeltaSec(ClientRequestT0PlatformSeconds, FirstChunkPlatformSeconds);
	const double DtClientToFirstAnimate = DeltaSec(ClientRequestT0PlatformSeconds, FirstAnimateSubchunkPlatformSeconds);
	const double DtClientToOnAnimStarted = DeltaSec(ClientRequestT0PlatformSeconds, FirstOnAnimationStartedPlatformSeconds);
	const double DtStreamToFirstPcm = DeltaSec(StreamStartPlatformSeconds, FirstChunkPlatformSeconds);
	const double DtStreamToOnAnimStarted = DeltaSec(StreamStartPlatformSeconds, FirstOnAnimationStartedPlatformSeconds);
	const double DtUtteranceWall = DeltaSec(StreamStartPlatformSeconds, FinishPlatformSeconds);

	UE_LOG(LogGodfreyPcmStream, Log,
		TEXT("[Godfrey latency summary] Utterance=%d | clientT0->firstHttp=%.4fs clientT0->firstPcmChunk=%.4fs clientT0->firstAnimate=%.4fs clientT0->OnAnimationStarted=%.4fs | streamT0->firstPcm=%.4fs streamT0->OnAnimationStarted=%.4fs | utterance_wall=%.4fs | "
			 "(ACE CreateA2FStream / provider send / first blendshape / AudioComponent::Play: ace.PipelineTimingLog 1 on ACERuntime)"),
		UtteranceOrdinal,
		DtClientToHttp,
		DtClientToFirstPcm,
		DtClientToFirstAnimate,
		DtClientToOnAnimStarted,
		DtStreamToFirstPcm,
		DtStreamToOnAnimStarted,
		DtUtteranceWall);
}

void UGodfreyPcmStreamSession::LogGodfreyAceStartupCompletionSummary(const double FinishPlatformSeconds) const
{
	if (!GetDefault<UUnrealPerformerGodfreySettings>()->bLogGodfreyAceStartupCompletionSummary)
	{
		return;
	}

	auto MsBetween = [](double From, double To) -> double
	{
		if (From < 0.0 || To < 0.0)
		{
			return -1.0;
		}
		return (To - From) * 1000.0;
	};

	const double T0 = UtteranceStartupMetrics.UtteranceT0PlatformSeconds;
	const double WallMs = (T0 > 0.0 && FinishPlatformSeconds > 0.0) ? (FinishPlatformSeconds - T0) * 1000.0 : -1.0;

	const double MsT0ToFirstPcm = MsBetween(T0, UtteranceStartupMetrics.FirstPcmChunkPlatformSeconds);
	const double MsPcmToAnimate = MsBetween(UtteranceStartupMetrics.FirstPcmChunkPlatformSeconds, UtteranceStartupMetrics.FirstAnimateFromAudioCallPlatformSeconds);
	const double MsT0ToOnAnim = MsBetween(T0, UtteranceStartupMetrics.AceOnAnimationStartedPlatformSeconds);
	const double MsAnimateToOnAnim = MsBetween(UtteranceStartupMetrics.FirstAnimateFromAudioCallPlatformSeconds, UtteranceStartupMetrics.AceOnAnimationStartedPlatformSeconds);

	const bool bFallbackHint = !UtteranceStartupMetrics.bAceOnAnimationStartedObserved;

	UE_LOG(LogGodfreyPcmStream, Log,
		TEXT("[Godfrey ACE startup summary] Utterance=%d wallMs=%.1f | OnAnimStarted=%s | t0->firstPcmMs=%.1f | firstPcm->firstAnimateMs=%.1f | firstAnimate->OnAnimMs=%.1f | t0->OnAnimMs=%.1f | bufferPriming=%s minBlendOverride=%s | if NO OnAnimStarted: check LogACERuntime for TryStart_Blocked / HoldPlay / ace.GodfreyStartupTiming=1"),
		UtteranceOrdinal,
		WallMs,
		UtteranceStartupMetrics.bAceOnAnimationStartedObserved ? TEXT("yes") : TEXT("NO"),
		MsT0ToFirstPcm,
		MsPcmToAnimate,
		MsAnimateToOnAnim,
		MsT0ToOnAnim,
		bGodfreyAceBufferLengthOverriddenThisUtterance ? TEXT("yes") : TEXT("no"),
		bGodfreyAceMinBlendOverriddenThisUtterance ? TEXT("yes") : TEXT("no"));

	if (bFallbackHint)
	{
		UE_LOG(LogGodfreyPcmStream, Warning,
			TEXT("[Godfrey ACE startup summary] Utterance=%d: OnAnimationStarted did NOT fire — correlate with ACE [ACE GodfreyStartup] lines (console: ace.GodfreyStartupTiming 1) and prior warnings in this utterance."),
			UtteranceOrdinal);
	}
}

void UGodfreyPcmStreamSession::ReportError(const FString& ErrorMessage)
{
	UE_LOG(LogGodfreyPcmStream, Error, TEXT("%s"), *ErrorMessage);
	OnError.Broadcast(ErrorMessage);
}
