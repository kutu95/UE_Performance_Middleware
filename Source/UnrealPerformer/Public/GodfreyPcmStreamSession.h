#pragma once

#include "UnrealPerformerApi.h"
#include "CoreMinimal.h"
#include "TimerManager.h"
#include "UObject/Object.h"
#include "GodfreyAceStartupDiagnostics.h"
#include "GodfreyPcmStreamSession.generated.h"

class AActor;
class UACEAudioCurveSourceComponent;
class UAudio2FaceParameters;
class UAudioComponent;
class USoundWaveProcedural;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FGodfreyStreamSimpleEvent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGodfreyStreamErrorEvent, const FString&, ErrorMessage);

/**
 * Streams PCM16 into NVIDIA ACE (AnimateFromAudioSamples).
 * Single clock: ACE internal playback is both the heard audio and the lipsync timing source.
 * Parallel WavUrl playback is disabled by default (see bGodfreyUseParallelPcmAudiblePlayback).
 */
UCLASS(BlueprintType)
class UNREAL_PERFORMER_API UGodfreyPcmStreamSession : public UObject
{
	GENERATED_BODY()

public:
	virtual void BeginDestroy() override;

	UFUNCTION(BlueprintCallable, Category = "Audio|Godfrey|Streaming", meta = (WorldContext = "WorldContextObject"))
	bool StartStream(UObject* WorldContextObject, AActor* CharacterForAce, FName ProviderName = FName("LocalA2F-Mark"), int32 SampleRate = 16000, int32 NumChannels = 1);

	UFUNCTION(BlueprintCallable, Category = "Audio|Godfrey|Streaming")
	bool PushPcm16Chunk(const TArray<uint8>& PcmBytes, FString& OutError);

	UFUNCTION(BlueprintCallable, Category = "Audio|Godfrey|Streaming")
	bool FinishStream(FString& OutError);

	UFUNCTION(BlueprintCallable, Category = "Audio|Godfrey|Streaming")
	void StopStream();

	/**
	 * Abort ingest, mute/stop ACE playback, then EndAudioSamples to close the A2X session.
	 * Call from EndPlay: a leftover LocalA2F session blocks the next PIE Allocate/PrepareNewAudioComponent.
	 * Flush may hitch the stop (unmatched backlog) — that is cheaper than a ~90s freeze on restart.
	 */
	UFUNCTION(BlueprintCallable, Category = "Audio|Godfrey|Streaming")
	static void AbortActiveStreamForCharacter(AActor* Character, const FString& Reason = TEXT("teardown"));

	/**
	 * Runs ~0.5s of silence through the same ACE path as streaming (AnimateFromAudioSamples + EndAudioSamples) to pay
	 * one-time costs (session / CUDA / model) before the first real utterance. Call from BeginPlay on the character.
	 * Silence duration should exceed DefaultEngine MaxInitialAudioChunkSize (often 0.5s) when testing non-burst pacing.
	 */
	UFUNCTION(BlueprintCallable, Category = "Audio|Godfrey|Streaming|Diagnostics")
	static bool WarmupAcePipeline(AActor* CharacterForAce, FName ProviderName = FName("LocalA2F-Mark"), int32 SampleRate = 16000, float SilenceDurationSeconds = 0.55f);

	/** Called by the streaming async action so latency summaries can include client request start. */
	void SetClientRequestT0PlatformSeconds(double PlatformSeconds);

	/** Optional Brain requestId for SpeechId correlation (set before StartStream when known). */
	void SetBrainRequestId(const FString& InRequestId);

	UFUNCTION(BlueprintPure, Category = "Audio|Godfrey|Streaming")
	FString GetSpeechId() const { return SpeechId; }

	UFUNCTION(BlueprintPure, Category = "Audio|Godfrey|Streaming")
	int32 GetUtteranceOrdinal() const { return UtteranceOrdinal; }

	/** Called when the first HTTP body bytes reach the game-thread PCM pipeline (first FIFO batch). */
	void NotifyFirstHttpBodyBytesPlatformSeconds(double PlatformSeconds);

	UFUNCTION(BlueprintPure, Category = "Audio|Godfrey|Streaming")
	int32 GetBufferedPcmBytes() const { return RollingPcmBytes.Num(); }

	/** Caps push budget during playback when sent audio outruns curves (optional soft throttle). */
	int32 GetEffectiveIngestPushBudget(int32 ConfigBudget, bool bAllowOverrun = false) const;

	/** Sent audio seconds minus max received curve timestamp (0 if unknown / not playing). */
	float GetUnmatchedAudioSeconds() const;

	/** Seconds of PCM already pushed to ACE (0 if stream not started). */
	float GetSentAudioSeconds() const;

	/** ACE procedural playback wall clock (-1 if unavailable). */
	float GetPlaybackWallSeconds() const;

	/**
	 * True while ACE is playing and curves lag sent audio enough that EndAudioSamples would hitch.
	 * Caller should keep polling until false (or timeout) before FinishStream.
	 */
	bool ShouldDeferEndAudioSamplesForCurveCatchUp() const;

	/**
	 * After HTTP drain, force EndAudioSamples once audible playback has caught sent PCM
	 * (any length), or near the end of a long occasion, or at the 120s stall deadline.
	 * Never force mid-speech with tens of seconds unmatched — that blocks the game thread.
	 */
	bool ShouldForceEndAudioSamplesDespiteCatchUpLag(float CatchUpElapsedSeconds) const;

	/** True once OnPlaybackEnded has broadcast for this utterance (OnAnimationEnded, watchdog, or stop). */
	bool HasAcePlaybackEnded() const { return bAcePlaybackEndedObserved; }

	/**
	 * True while this character has an active ACE stream session whose audible playback has not ended yet.
	 * Covers the gap after HTTP FinishStream (IsStreaming=false) while ACE still plays — use for mic mute.
	 */
	UFUNCTION(BlueprintPure, Category = "Audio|Godfrey|Streaming")
	static bool IsCharacterAudiblePlaybackActive(const AActor* Character);

	/** Fires when ACE internal playback/sync pipeline starts (UACEAudioCurveSourceComponent::OnAnimationStarted). */
	UPROPERTY(BlueprintAssignable, Category = "Audio|Godfrey|Streaming")
	FGodfreyStreamSimpleEvent OnPlaybackStarted;

	UPROPERTY(BlueprintAssignable, Category = "Audio|Godfrey|Streaming")
	FGodfreyStreamSimpleEvent OnLipSyncStarted;

	/** Fires when ACE audible playback completes (UACEAudioCurveSourceComponent::OnAnimationEnded), not when HTTP ingest finishes. */
	UPROPERTY(BlueprintAssignable, Category = "Audio|Godfrey|Streaming")
	FGodfreyStreamSimpleEvent OnPlaybackEnded;

	/**
	 * HTTP is still open (FinishStream not called) but playback has exhausted the PCM we have
	 * and ingest has been quiet for GodfreyAceIngestStallTimeoutSeconds. The async action
	 * should cancel the hung request and FinishStream so lips/body can rest.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Audio|Godfrey|Streaming")
	FGodfreyStreamSimpleEvent OnIngestStallWhileAudioCaughtUp;

	UPROPERTY(BlueprintAssignable, Category = "Audio|Godfrey|Streaming")
	FGodfreyStreamSimpleEvent OnFinished;

	UPROPERTY(BlueprintAssignable, Category = "Audio|Godfrey|Streaming")
	FGodfreyStreamErrorEvent OnError;

private:
	bool ValidateFormat(const TArray<uint8>& PcmBytes, FString& OutError) const;
	/** In-place linear gain with soft saturation near full scale; returns samples that entered the limiter knee. */
	static int64 ApplySpeechGainToPcm16(TArray<uint8>& PcmBytes, float Gain);
	static int64 ApplySpeechSilenceGateToPcm16(TArray<uint8>& PcmBytes, int32 GateAbs);
	static int32 FindLastNonSilentSampleIndex(const TArray<uint8>& PcmBytes, int32 GateAbs);
	/** Last sample at the end of a 20ms window whose RMS is still speech, not breath/decay. */
	static int32 FindLastVoicedWindowEndIndex(const TArray<uint8>& PcmBytes, int32 SampleRate, int32 MinRms);
	UAudio2FaceParameters* GetOrCreateAceFaceParameters();
	void ReportError(const FString& ErrorMessage);
	void UnbindAceDelegates();
	void RegisterAsActiveAceSessionForCharacter();
	void UnregisterActiveAceSessionForCharacter();
	bool IsActiveAceSessionForCharacter() const;
	void CancelDeferredAceUnbind();
	void ScheduleDeferredAceUnbindAfterFinishStream(UWorld* World, double FinishStreamPlatformSeconds);
	/** Poll ACE wall/MaxCurveTs for playback end; safe to call from OnAnimationStarted (before FinishStream). */
	void EnsureAudioEndWatchdogScheduled(UWorld* World);
	void ProcessDeferredAceUnbindTick();
	void LogUtteranceLatencySummaryAtFinishIfEnabled(double FinishPlatformSeconds) const;
	void ApplyGodfreyAcePlaybackPriming(UACEAudioCurveSourceComponent* AceComp);
	void RestoreGodfreyAcePlaybackPrimingIfApplied();
	void EnsureParallelAudibleWave(AActor* Character);
	void PrepareFreshParallelAudibleWave(AActor* Character);
	void AbortParallelAudiblePlaybackForAceResync(bool bRestoreAceVolume);
	void QueueParallelAudiblePcm(const TArray<uint8>& PcmBytes);
	void TryStartParallelAudiblePlayback(bool bIgnoreBufferThreshold = false, bool bAceSyncStart = false);
	void StopParallelAudiblePlayback(bool bRestoreAceVolume);
	void MuteAceVolumeForParallelLipSyncOnly();
	void UpdateParallelAudibleWaveDuration();
	void UpsamplePcm16MonoForAudiblePlayback(const TArray<uint8>& SourcePcm, int32 SourceSampleRate, TArray<uint8>& OutPcm, int32& OutSampleRate) const;
	int32 GetParallelAudibleEffectiveSampleRate() const;
	void LogGodfreyAceStartupCompletionSummary(double FinishPlatformSeconds) const;
	void LogAudiblePlaybackDiagnostics(const TCHAR* ContextLabel) const;
	void ScheduleAudibleDiagnosticsTimer(UWorld* World);
	void CancelAudibleDiagnosticsTimer();
	void AudibleDiagnosticsTimerTick();
	void BindParallelAudibleUnderflowDelegate();
	void UnbindParallelAudibleUnderflowDelegate();
	void HandleParallelProceduralUnderflow(USoundWaveProcedural* Wave, int32 SamplesRequired);

	UFUNCTION()
	void HandleAceAnimationStarted();

	UFUNCTION()
	void HandleAceAnimationEnded();

	/** Shared end path for native OnAnimationEnded or wall-clock/MaxCurveTs watchdog. */
	void CompleteAcePlaybackEnded(const TCHAR* Reason);

	/**
	 * End utterance when ACE audio timeline is done even if OnAnimationEnded never fires.
	 * Armed from OnAnimationStarted (not only FinishStream) so Speaking ends when audible audio ends.
	 */
	bool TryCompleteAcePlaybackFromAudioEndWatchdog();

	/**
	 * While HTTP is still open: if playback has exhausted sent PCM and ingest has been quiet
	 * long enough, broadcast OnIngestStallWhileAudioCaughtUp once so the async action can FinishStream.
	 */
	void TryBroadcastIngestStallIfPlaybackExhausted(
		double Now,
		float Wall,
		float EffectiveWall,
		double ExpectedFromSamples,
		bool bProceduralPlaying,
		float MaxTs,
		bool bMaxTsStable);

	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> TargetCharacter;

	UPROPERTY(Transient)
	TArray<uint8> RollingPcmBytes;

	FName AceProviderName = NAME_None;
	int32 StreamSampleRate = 0;
	int32 StreamNumChannels = 0;
	int64 TotalSamplesSentToAce = 0;
	double LastSamplePushPlatformSeconds = -1.0;
	float LastObservedMaxCurveTs = -1.f;
	double MaxCurveTsLastChangePlatformSeconds = -1.0;
	/** Highest procedural playback wall clock seen; ACE reports -1 once playback stops. */
	float LastPositiveProceduralWallSeconds = -1.f;
	int64 SpeechGainSaturatedSampleCount = 0;
	int64 SpeechSilenceGatedSampleCount = 0;
	/** Samples through the last voiced 20ms window (RMS floor). Trailing breath above the sample gate is not voice. */
	int64 LastNonSilentSamplesSentToAce = 0;
	/** Last sample >= silence gate; diagnostic only (late vs perceived end of speech). */
	int64 LastGate750SamplesSentToAce = 0;
	int32 SpeechPeakAbsSentToAce = 0;
	int32 LastVoiceRmsFloorUsed = 0;
	bool bLoggedAceFaceParamsThisUtterance = false;
	bool bLoggedAceRestPoseThisUtterance = false;
	bool bBroadcastIngestStallThisUtterance = false;

	UPROPERTY(Transient)
	TObjectPtr<UAudio2FaceParameters> AceFaceParameters;
	bool bStreamStarted = false;
	bool bFinished = false;
	bool bLoggedFirstPcmChunk = false;
	bool bLoggedSpeechGainThisUtterance = false;
	bool bBoundAceAnimationStarted = false;
	bool bBoundAceAnimationEnded = false;
	bool bAcePlaybackEndedObserved = false;

	double FirstChunkWorldTimeSeconds = -1.0;
	double FirstChunkPlatformSeconds = -1.0;

	int32 UtteranceOrdinal = 0;
	FString SpeechId;
	FString BrainRequestId;
	double ClientRequestT0PlatformSeconds = -1.0;
	double StreamStartPlatformSeconds = -1.0;
	double FirstHttpBodyBytesPlatformSeconds = -1.0;
	double FirstAnimateSubchunkPlatformSeconds = -1.0;
	double FirstOnAnimationStartedPlatformSeconds = -1.0;

	bool bGodfreyAcePrimingApplied = false;
	bool bGodfreySavedAceBufferLength = false;
	float GodfreySavedAceBufferLengthInSeconds = 0.1f;
	bool bGodfreySavedAceMinBlend = false;
	int32 GodfreySavedAceMinBlendShapeSamplesBeforePlay = 1;
	bool bGodfreySavedAceMinCurveLead = false;
	float GodfreySavedAceMinCurveTimestampBeforePlay = 0.f;
	bool bGodfreyAceBufferLengthOverriddenThisUtterance = false;
	bool bGodfreyAceMinBlendOverriddenThisUtterance = false;
	bool bGodfreyAceMinCurveLeadOverriddenThisUtterance = false;
	FGodfreyAceUtteranceStartupMetrics UtteranceStartupMetrics;

	FTimerHandle DeferredAceUnbindTimerHandle;
	TWeakObjectPtr<UWorld> DeferredAceUnbindWorld;
	bool bDeferredAceUnbindActive = false;
	int32 DeferredUnbindUtteranceOrdinal = 0;
	double DeferredUnbindStartPlatformSeconds = 0.0;
	double DeferredUnbindFinishStreamPlatformSeconds = 0.0;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> ParallelAudibleAudioComponent;

	UPROPERTY(Transient)
	TObjectPtr<USoundWaveProcedural> ParallelAudibleWave;

	int32 ParallelAudibleQueuedBytes = 0;
	bool bParallelAudiblePlaybackStarted = false;
	bool bParallelAudibleActive = false;
	bool bAceVolumeMutedForParallelLipSync = false;
	bool bSavedAceVolumeForParallelMute = false;
	float SavedAceVolumeBeforeParallelMute = 1.f;

	FTimerHandle AudibleDiagnosticsTimerHandle;
	TWeakObjectPtr<UWorld> AudibleDiagnosticsWorld;
	int32 ParallelAudibleQueueCallCount = 0;
	int32 ParallelProceduralUnderflowCount = 0;
	int32 AudibleDiagnosticsTickCount = 0;
	bool bLoggedFirstParallelQueue = false;
	bool bLoggedFirstNonSilentParallelQueue = false;
};
