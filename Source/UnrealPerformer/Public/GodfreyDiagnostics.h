#pragma once

#include "UnrealPerformerApi.h"
#include "CoreMinimal.h"
#include "Logging/LogMacros.h"
#include "Subsystems/WorldSubsystem.h"
#include "GodfreyDiagnostics.generated.h"

/** Structured Godfrey pipeline channels (Phase 1 hardening). */
DECLARE_LOG_CATEGORY_EXTERN(LogGodfreySpeech, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LogGodfreyACE, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LogGodfreyAnimation, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LogGodfreyBehaviour, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LogGodfreyAudio, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LogGodfreyQueue, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LogGodfreyVision, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LogGodfreyPerfMonitor, Log, All);

/** Pipeline stages for utterance timing (milliseconds from SpeechId T0). */
UENUM(BlueprintType)
enum class EGodfreyUtteranceStage : uint8
{
	SpeechGenerated UMETA(DisplayName = "Speech Generated"),
	AudioReady UMETA(DisplayName = "Audio Ready"),
	AceStarted UMETA(DisplayName = "ACE Started"),
	AudioPlaybackStarted UMETA(DisplayName = "Audio Playback Started"),
	FirstAudibleSample UMETA(DisplayName = "First Audible Sample"),
	BehaviourStarted UMETA(DisplayName = "Behaviour Started"),
	SpeechFinished UMETA(DisplayName = "Speech Finished"),
	BehaviourFinished UMETA(DisplayName = "Behaviour Finished"),
	BodyAnimStarted UMETA(DisplayName = "Body Animation Started"),
	BodyAnimEnded UMETA(DisplayName = "Body Animation Ended"),
	AceComplete UMETA(DisplayName = "ACE Complete"),
	AudioQueued UMETA(DisplayName = "Audio Queued"),
};

USTRUCT(BlueprintType)
struct UNREAL_PERFORMER_API FGodfreyUtteranceTimingRecord
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Godfrey|Diagnostics")
	FString SpeechId;

	UPROPERTY(BlueprintReadOnly, Category = "Godfrey|Diagnostics")
	int32 UtteranceOrdinal = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Godfrey|Diagnostics")
	FString BrainRequestId;

	/** Platform time when SpeechId was created / stream started. */
	UPROPERTY(BlueprintReadOnly, Category = "Godfrey|Diagnostics")
	double T0PlatformSeconds = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "Godfrey|Diagnostics")
	double AudioReceivedMs = -1.0;

	UPROPERTY(BlueprintReadOnly, Category = "Godfrey|Diagnostics")
	double AudioQueuedMs = -1.0;

	UPROPERTY(BlueprintReadOnly, Category = "Godfrey|Diagnostics")
	double AceProcessingBeginsMs = -1.0;

	UPROPERTY(BlueprintReadOnly, Category = "Godfrey|Diagnostics")
	double AceCompleteMs = -1.0;

	UPROPERTY(BlueprintReadOnly, Category = "Godfrey|Diagnostics")
	double AudioPlaybackBeginsMs = -1.0;

	UPROPERTY(BlueprintReadOnly, Category = "Godfrey|Diagnostics")
	double FirstAudibleSampleMs = -1.0;

	UPROPERTY(BlueprintReadOnly, Category = "Godfrey|Diagnostics")
	double SpeechCompleteMs = -1.0;

	UPROPERTY(BlueprintReadOnly, Category = "Godfrey|Diagnostics")
	double BehaviourStartedMs = -1.0;

	UPROPERTY(BlueprintReadOnly, Category = "Godfrey|Diagnostics")
	double BehaviourFinishedMs = -1.0;

	UPROPERTY(BlueprintReadOnly, Category = "Godfrey|Diagnostics")
	double BodyAnimStartedMs = -1.0;

	UPROPERTY(BlueprintReadOnly, Category = "Godfrey|Diagnostics")
	double BodyAnimEndedMs = -1.0;
};

/**
 * World subsystem: utterance SpeechId correlation, structured stage logs, and debug HUD snapshot.
 * Additive only — does not change speech/ACE/animation behaviour.
 */
UCLASS()
class UNREAL_PERFORMER_API UGodfreyDiagnosticsSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	/** Begin a new utterance trace. Prefer BrainRequestId when available. */
	UFUNCTION(BlueprintCallable, Category = "Godfrey|Diagnostics")
	FString BeginUtterance(int32 UtteranceOrdinal, const FString& BrainRequestId = FString());

	UFUNCTION(BlueprintCallable, Category = "Godfrey|Diagnostics")
	void MarkStage(const FString& SpeechId, EGodfreyUtteranceStage Stage);

	UFUNCTION(BlueprintCallable, Category = "Godfrey|Diagnostics")
	void MarkStageForCurrent(EGodfreyUtteranceStage Stage);

	UFUNCTION(BlueprintPure, Category = "Godfrey|Diagnostics")
	FString GetCurrentSpeechId() const { return CurrentSpeechId; }

	UFUNCTION(BlueprintPure, Category = "Godfrey|Diagnostics")
	int32 GetCurrentUtteranceOrdinal() const { return CurrentRecord.UtteranceOrdinal; }

	UFUNCTION(BlueprintPure, Category = "Godfrey|Diagnostics")
	FGodfreyUtteranceTimingRecord GetCurrentTimingRecord() const { return CurrentRecord; }

	UFUNCTION(BlueprintCallable, Category = "Godfrey|Diagnostics")
	void SetBehaviourStateName(const FString& StateName);

	UFUNCTION(BlueprintCallable, Category = "Godfrey|Diagnostics")
	void SetCurrentAnimationName(const FString& AnimName);

	UFUNCTION(BlueprintCallable, Category = "Godfrey|Diagnostics")
	void SetCurrentAnimationContext(const FString& ContextName);

	UFUNCTION(BlueprintCallable, Category = "Godfrey|Diagnostics")
	void SetQueueLength(int32 Length);

	UFUNCTION(BlueprintCallable, Category = "Godfrey|Diagnostics")
	void SetSpeechLatencyMs(float LatencyMs);

	UFUNCTION(BlueprintPure, Category = "Godfrey|Diagnostics")
	FString GetBehaviourStateName() const { return BehaviourStateName; }

	UFUNCTION(BlueprintPure, Category = "Godfrey|Diagnostics")
	FString GetCurrentAnimationName() const { return CurrentAnimationName; }

	UFUNCTION(BlueprintPure, Category = "Godfrey|Diagnostics")
	FString GetCurrentAnimationContext() const { return CurrentAnimationContext; }

	UFUNCTION(BlueprintPure, Category = "Godfrey|Diagnostics")
	int32 GetQueueLength() const { return QueueLength; }

	UFUNCTION(BlueprintPure, Category = "Godfrey|Diagnostics")
	float GetSpeechLatencyMs() const { return SpeechLatencyMs; }

	/** Log a one-line timing summary for the current utterance (ms). */
	UFUNCTION(BlueprintCallable, Category = "Godfrey|Diagnostics")
	void LogTimingSummary(const FString& SpeechId) const;

	static UGodfreyDiagnosticsSubsystem* Get(const UObject* WorldContext);

private:
	void ApplyStageTimestamp(FGodfreyUtteranceTimingRecord& Record, EGodfreyUtteranceStage Stage, double ElapsedMs) const;
	static const TCHAR* StageToChannel(EGodfreyUtteranceStage Stage);
	static const TCHAR* StageToLabel(EGodfreyUtteranceStage Stage);

	FString CurrentSpeechId;
	FGodfreyUtteranceTimingRecord CurrentRecord;
	TMap<FString, FGodfreyUtteranceTimingRecord> RecordsBySpeechId;

	FString BehaviourStateName = TEXT("Idle");
	FString CurrentAnimationName = TEXT("(none)");
	FString CurrentAnimationContext;
	FString CurrentEmotionName = TEXT("(reserved)");
	int32 QueueLength = 0;
	float SpeechLatencyMs = -1.f;
};
