#pragma once



#include "UnrealPerformerApi.h"
#include "CoreMinimal.h"

#include "HAL/CriticalSection.h"

#include "Interfaces/IHttpRequest.h"

#include "Interfaces/IHttpResponse.h"

#include "Kismet/BlueprintAsyncActionBase.h"

#include "TimerManager.h"



#include <atomic>



#include "AsyncActionStreamGodfreySpeech.generated.h"



class UGodfreyPcmStreamSession;

class AActor;

class FJsonObject;



DECLARE_DYNAMIC_MULTICAST_DELEGATE(FGodfreySpeechStreamEvent);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGodfreySpeechStreamErrorEvent, const FString&, ErrorMessage);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FGodfreyPerformanceCueEvent, const FString&, CueType, const FString&, CueValue, const FString&, RawCue);



UCLASS(BlueprintType)

class UNREAL_PERFORMER_API UAsyncActionStreamGodfreySpeech : public UBlueprintAsyncActionBase

{

	GENERATED_BODY()



public:

	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", DisplayName = "StreamGodfreySpeechToAudio"), Category = "Audio|Godfrey|Streaming")

	static UAsyncActionStreamGodfreySpeech* StreamGodfreySpeechToAudio(

		UObject* WorldContextObject,

		const FString& PromptText,

		const FString& GodfreyBrainBaseUrl,

		AActor* CharacterForAce,

		FName ProviderName = FName("LocalA2F-Mark"),

		int32 SampleRate = 16000,

		int32 NumChannels = 1);



	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", DisplayName = "PullQueuedGodfreySpeechToAudio"), Category = "Audio|Godfrey|Streaming")

	static UAsyncActionStreamGodfreySpeech* PullQueuedGodfreySpeechToAudio(

		UObject* WorldContextObject,

		const FString& GodfreyBrainBaseUrl,

		AActor* CharacterForAce,

		FName ProviderName = FName("LocalA2F-Mark"),

		int32 SampleRate = 24000,

		int32 NumChannels = 1);



	virtual void Activate() override;

	/** Cancel HTTP + ACE ingest immediately (PIE stop / EndPlay). Does not broadcast OnError. */
	UFUNCTION(BlueprintCallable, Category = "Audio|Godfrey|Streaming")
	void Cancel();

	/** Call before Activate() so ACE ingests but does not Play until ReleaseAudibleHold. */
	void SetHoldAudibleUntilReleased(bool bHold) { bHoldAudibleUntilReleased = bHold; }



	UPROPERTY(BlueprintAssignable)

	FGodfreySpeechStreamEvent OnPlaybackStarted;



	UPROPERTY(BlueprintAssignable)

	FGodfreySpeechStreamEvent OnLipSyncStarted;



	UPROPERTY(BlueprintAssignable)

	FGodfreySpeechStreamEvent OnFinished;



	UPROPERTY(BlueprintAssignable)

	FGodfreySpeechStreamErrorEvent OnError;



	UPROPERTY(BlueprintAssignable, Category = "Audio|Godfrey|Streaming", meta = (DisplayName = "On No Queue"))

	FGodfreySpeechStreamEvent OnNoQueue;



	UPROPERTY(BlueprintAssignable, Category = "Audio|Godfrey|Streaming", meta = (DisplayName = "On Performance Cue"))

	FGodfreyPerformanceCueEvent OnPerformanceCue;



private:

	UFUNCTION()

	void HandleSessionPlaybackStarted();



	UFUNCTION()

	void HandleSessionLipSyncStarted();



	UFUNCTION()

	void HandleSessionPlaybackEnded();



	UFUNCTION()

	void HandleSessionIngestStallWhileAudioCaughtUp();



	UFUNCTION()

	void HandleSessionError(const FString& ErrorMessage);



	void StartExhibitionTtsStatusGet();

	void HandleExhibitionTtsStatusCompleted(FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bConnectedSuccessfully);



	void LogGodfreyPerformanceEventsFromStatusJson(const TSharedPtr<FJsonObject>& JsonObj);

	/** Brain conversationEnd flag: latch the farewell so Unreal runs it after this reply is spoken. */
	void TryLatchConversationEndFromStatusJson(const TSharedPtr<FJsonObject>& JsonObj);

	/** Direct stream-pcm path: Brain sets X-Godfrey-Conversation-End on the PCM response. */
	void TryLatchConversationEndFromStreamHeaders(const FHttpResponsePtr& Response);
	void LatchConversationEndFromBrain(const FString& Source);



	void TryForwardUtteranceStartedToPerformerIfNeeded();

	void TryForwardUtteranceEndedToPerformerIfNeeded();

	void TryForwardPerformanceCueToPerformer(const FString& CueType, const FString& CueValue, const FString& RawCue);



	void StartSpeakStreamPcmPost();

	void HandleRequestProgress64(uint64 BytesReceived);

	void HandleRequestCompleted(bool bConnectedSuccessfully, int32 ResponseCode, const FString& CompletionError,
		FHttpResponsePtr Response);

	void ProcessPendingPcmBytes(bool bFlushFinal, int32* OptMaxPushesRemaining = nullptr);

	bool HasAlignedPcmPending() const;

	void ScheduleHttpBodyDrain();

	void ScheduleHttpBodyDrainDelayed(float DelaySeconds);

	void ProcessHttpBodyFifo_GameThread();

	void TryFinishStreamAfterHttpComplete();

	float GetHttpBodyDrainPaceDelaySeconds() const;

	void CompletePullQueuedActionAfterPlayback();

	void FailAndStop(const FString& ErrorMessage);

	FString BuildStreamUrl() const;

	FString BuildExhibitionTtsStatusUrl() const;

	FString BuildLivePerformanceEventsUrl() const;

	void StartLivePerformanceCuePoll();

	void StopLivePerformanceCuePoll();

	void PollLivePerformanceEventsOnce();

	void IngestPerformanceEventsFromJson(const TSharedPtr<FJsonObject>& JsonObj);



	UPROPERTY()

	TObjectPtr<UObject> WorldContextObject = nullptr;



	UPROPERTY()

	TObjectPtr<AActor> CharacterForAce = nullptr;



	UPROPERTY()

	TObjectPtr<UGodfreyPcmStreamSession> StreamSession = nullptr;



	FString PromptText;

	FString GodfreyBrainBaseUrl;

	FName ProviderName = FName("LocalA2F-Mark");

	int32 SampleRate = 16000;

	int32 NumChannels = 1;



	bool bPullQueuedMode = false;
	bool bHoldAudibleUntilReleased = false;

	FString QueuedTtsRequestId;

	FString SpeakRequestId;

	int32 PerformanceEventsForwarded = 0;

	bool bLivePerformanceEventsClosed = false;

	bool bPerformanceCuePollInFlight = false;

	FTimerHandle PerformanceCuePollTimerHandle;



	TSharedPtr<class IHttpRequest, ESPMode::ThreadSafe> ActiveRequest;

	bool bDidForwardUtteranceStartedToPerformer = false;

	bool bDidForwardUtteranceEndedToPerformer = false;

	bool bSawFirstHttpProgress = false;

	bool bLoggedFormat = false;

	bool bDidFinish = false;

	bool bHttpCompleteAwaitingFinish = false;

	/** Hung Brain/TTS: abandon the HTTP receive and FinishStream with the PCM we already have. */
	bool bForcedFinishFromIngestStall = false;

	/** True while waiting on a paced/catch-up timer before next drain (avoids busy-loop AsyncTask). */
	bool bHttpBodyDrainCatchUpDeferred = false;

	/** Pull-queued mode: HTTP ingest done; defer OnFinished until ACE playback ends. */
	bool bAwaitingPlaybackBeforeFinish = false;

	/**
	 * OnFinished has broadcast but ACE is still playing, so SetReadyToDestroy is held back.
	 * Releasing the action here would drop the last reference to StreamSession, and garbage
	 * collection would silently cancel its end-of-playback watchdog mid-utterance.
	 */
	bool bAwaitingPlaybackBeforeDestroy = false;

	double AceEndCatchUpWaitStartPlatformSeconds = -1.0;
	double LastAceCatchUpWaitLogPlatformSeconds = -1.0;

	FTimerHandle HttpBodyDrainTimerHandle;

	TArray<uint8> PendingBytes;



	double FirstHttpProgressPlatformSeconds = -1.0;

	double FirstStreamDelegatePlatformSeconds = -1.0;

	double FirstGameThreadPcmPlatformSeconds = -1.0;

	int32 HttpStreamChunkCount = 0;

	int64 TotalHttpBodyBytesReceived = 0;

	int32 AnimateFromAudioPushCount = 0;



	FCriticalSection HttpBodyLock;

	TArray<uint8> HttpBodyAccum;

	std::atomic<bool> bHttpBodyDrainPending{ false };

};


