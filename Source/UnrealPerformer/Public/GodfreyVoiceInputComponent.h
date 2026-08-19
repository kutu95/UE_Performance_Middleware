#pragma once

#include "UnrealPerformerApi.h"
#include "Components/ActorComponent.h"
#include "GodfreyVoiceInputComponent.generated.h"

class IWebSocket;
class UGodfreyDirectSpeechComponent;
class UGodfreyExhibitionQueuePollComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGodfreyVoiceTranscriptEvent, const FString&, Transcript);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGodfreyVoiceInputErrorEvent, const FString&, ErrorMessage);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FGodfreyVoiceInputSimpleEvent);

/**
 * Always-on exhibition microphone for Unreal.
 *
 * Streams PCM16 mono @ 24 kHz to Godfrey Brain ws://.../api/unreal/stt
 * (OpenAI Realtime transcription + server_vad). On final transcript, calls
 * UGodfreyDirectSpeechComponent::AskGodfrey so the existing stream-pcm reply
 * path is reused.
 *
 * Does not replace the browser Web Speech path — keep ExhibitionQueuePoll for that.
 */
UCLASS(ClassGroup = (Godfrey), meta = (BlueprintSpawnableComponent))
class UNREAL_PERFORMER_API UGodfreyVoiceInputComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UGodfreyVoiceInputComponent();
	virtual ~UGodfreyVoiceInputComponent();

	UFUNCTION(BlueprintCallable, Category = "Godfrey|Voice Input")
	void StartListening();

	UFUNCTION(BlueprintCallable, Category = "Godfrey|Voice Input")
	void StopListening();

	UFUNCTION(BlueprintCallable, Category = "Godfrey|Voice Input")
	void SetMicPaused(bool bPaused);

	UFUNCTION(BlueprintPure, Category = "Godfrey|Voice Input")
	bool IsListening() const { return bListening; }

	UFUNCTION(BlueprintPure, Category = "Godfrey|Voice Input")
	bool IsMicPaused() const { return bMicPaused; }

	/** True when a visitor utterance would be accepted (mic live, Godfrey not speaking, past post-speech ignore). */
	UFUNCTION(BlueprintPure, Category = "Godfrey|Voice Input")
	bool CanVisitorSpeak() const;

	/** True while Brain STT reports an in-progress visitor utterance (server_vad speech_started). */
	UFUNCTION(BlueprintPure, Category = "Godfrey|Voice Input")
	bool IsVisitorSpeechInProgress() const { return bSpeechActive; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Voice Input")
	FString GodfreyBrainBaseUrl = TEXT("http://localhost:3000");

	/** If unset, uses GodfreyDirectSpeech on the same owner. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Voice Input")
	TObjectPtr<UGodfreyDirectSpeechComponent> DirectSpeech = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Voice Input")
	bool bListenOnBeginPlay = true;

	/** Pause STT while Godfrey is speaking (DirectSpeech or exhibition queue stream). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Voice Input")
	bool bPauseWhileGodfreySpeaking = true;

	/**
	 * Keep the mic paused after audible speech ends so speaker tail / room reverb is not
	 * transcribed as a visitor turn. 0 = open Speak immediately on playback complete.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Voice Input", meta = (ClampMin = "0.0", ClampMax = "8.0"))
	float PostSpeechIgnoreSeconds = 1.25f;

	/**
	 * Substring match against capture device display name (e.g. "HyperX").
	 * Empty = system default mic (often the webcam — avoid that when a dedicated mic is plugged in).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Voice Input")
	FString PreferredCaptureDeviceNameFilter = TEXT("HyperX");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Voice Input", meta = (ClampMin = "8000", ClampMax = "48000"))
	int32 CaptureSampleRate = 48000;

	/** OpenAI Realtime transcription expects 24 kHz PCM16 mono. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Godfrey|Voice Input")
	int32 StreamSampleRate = 24000;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Voice Input", meta = (ClampMin = "1", ClampMax = "8"))
	int32 CaptureNumChannels = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Voice Input", meta = (ClampMin = "1", ClampMax = "64"))
	int32 MinTranscriptChars = 2;

	/**
	 * After speech_stopped, wait this long for a usable transcript_completed.
	 * If none (or empty/echo), AskGodfrey with MissedTranscriptPrompt so Godfrey asks the visitor to repeat.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Voice Input", meta = (ClampMin = "0.5", ClampMax = "8.0", EditCondition = "bEnableMissedTranscriptPrompt"))
	float MissedTranscriptTimeoutSeconds = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Voice Input")
	bool bEnableMissedTranscriptPrompt = true;

	/** Instruction for Brain when STT heard speech but produced no usable transcript. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Voice Input", meta = (MultiLine = "true", EditCondition = "bEnableMissedTranscriptPrompt"))
	FString MissedTranscriptPrompt = TEXT(
		"(The visitor just spoke, but their words were not transcribed. In one short sentence, apologise that you did not catch that and ask them to please repeat. Stay in character as Captain Godfrey. Do not invent what they said. Do not say goodbye.)");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Voice Input", meta = (ClampMin = "0.5", ClampMax = "30.0"))
	float ReconnectDelaySeconds = 2.0f;

	/** True after speech_started until a usable final transcript or a missed-transcript reply is fired. */
	UFUNCTION(BlueprintPure, Category = "Godfrey|Voice Input")
	bool IsAwaitingVisitorTranscript() const { return bAwaitingFinalTranscript; }

	UPROPERTY(BlueprintAssignable, Category = "Godfrey|Voice Input")
	FGodfreyVoiceInputSimpleEvent OnSpeechStarted;

	UPROPERTY(BlueprintAssignable, Category = "Godfrey|Voice Input")
	FGodfreyVoiceInputSimpleEvent OnSpeechStopped;

	UPROPERTY(BlueprintAssignable, Category = "Godfrey|Voice Input")
	FGodfreyVoiceTranscriptEvent OnTranscriptDelta;

	UPROPERTY(BlueprintAssignable, Category = "Godfrey|Voice Input")
	FGodfreyVoiceTranscriptEvent OnTranscriptCompleted;

	UPROPERTY(BlueprintAssignable, Category = "Godfrey|Voice Input")
	FGodfreyVoiceInputErrorEvent OnVoiceInputError;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	void EnsureDirectSpeech();
	bool IsGodfreySpeaking() const;
	bool IsInPostSpeechIgnore() const;
	int32 ResolveCaptureDeviceIndex() const;
	void ConnectSttWebSocket();
	void DisconnectSttWebSocket();
	void SendControl(const FString& Action);
	void FlushCaptureToSocket();
	void HandleSocketMessage(const FString& Message);
	void HandleFinalTranscript(const FString& Transcript);
	void QueueOrSendVisitorTranscript(const FString& Transcript, bool bIsFarewell);
	void TrySendPendingFarewellTranscript();
	void ArmAwaitingFinalTranscript();
	void ClearAwaitingFinalTranscript();
	void ScheduleMissedTranscriptWatch();
	void HandleTranscriptMissed(const FString& Reason);
	bool TryFireMissedTranscriptPrompt(const FString& Reason);
	void ScheduleReconnect();
	void OnCaptureAudio(const float* InAudio, int32 NumFrames, int32 NumChannels, int32 SampleRate);
	void AppendResampledPcm16(const float* InAudio, int32 NumFrames, int32 NumChannels, int32 InSampleRate);

	bool OpenCaptureStream();
	void CloseCaptureStream();

	TSharedPtr<IWebSocket> SttSocket;
	FCriticalSection CaptureMutex;
	TArray<uint8> PendingPcm16;

	bool bListening = false;
	bool bMicPaused = false;
	bool bServerReady = false;
	bool bSpeechActive = false;
	bool bAwaitingFinalTranscript = false;
	bool bReconnectScheduled = false;
	bool bWantListening = false;
	bool bWasGodfreySpeaking = false;
	bool bWasCanVisitorSpeak = false;
	double PostSpeechIgnoreUntilWorldTime = -1.0;
	FString PendingFarewellTranscript;

	FTimerHandle ReconnectTimerHandle;
	FTimerHandle MissedTranscriptTimerHandle;

	/** Opaque Audio::FAudioCapture held without exposing AudioCaptureCore in the header. */
	struct FGodfreyAudioCaptureState* CaptureState = nullptr;
};
