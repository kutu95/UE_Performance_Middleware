#include "GodfreyDiagnostics.h"

#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "UnrealPerformerGodfreySettings.h"

DEFINE_LOG_CATEGORY(LogGodfreySpeech);
DEFINE_LOG_CATEGORY(LogGodfreyACE);
DEFINE_LOG_CATEGORY(LogGodfreyAnimation);
DEFINE_LOG_CATEGORY(LogGodfreyBehaviour);
DEFINE_LOG_CATEGORY(LogGodfreyAudio);
DEFINE_LOG_CATEGORY(LogGodfreyQueue);
DEFINE_LOG_CATEGORY(LogGodfreyVision);
DEFINE_LOG_CATEGORY(LogGodfreyPerfMonitor);

void UGodfreyDiagnosticsSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UE_LOG(LogGodfreyPerfMonitor, Log, TEXT("[Performance] Godfrey diagnostics subsystem initialized (Phase 1 hardening)."));
}

void UGodfreyDiagnosticsSubsystem::Deinitialize()
{
	RecordsBySpeechId.Reset();
	CurrentSpeechId.Reset();
	Super::Deinitialize();
}

bool UGodfreyDiagnosticsSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (const UWorld* World = Cast<UWorld>(Outer))
	{
		return World->IsGameWorld();
	}
	return false;
}

UGodfreyDiagnosticsSubsystem* UGodfreyDiagnosticsSubsystem::Get(const UObject* WorldContext)
{
	if (!WorldContext)
	{
		return nullptr;
	}
	const UWorld* World = WorldContext->GetWorld();
	return World ? World->GetSubsystem<UGodfreyDiagnosticsSubsystem>() : nullptr;
}

FString UGodfreyDiagnosticsSubsystem::BeginUtterance(int32 UtteranceOrdinal, const FString& BrainRequestId)
{
	FGodfreyUtteranceTimingRecord Record;
	Record.UtteranceOrdinal = UtteranceOrdinal;
	Record.BrainRequestId = BrainRequestId;
	Record.T0PlatformSeconds = FPlatformTime::Seconds();

	if (!BrainRequestId.IsEmpty())
	{
		Record.SpeechId = FString::Printf(TEXT("utt-%d-%s"), UtteranceOrdinal, *BrainRequestId.Left(8));
	}
	else
	{
		Record.SpeechId = FString::Printf(TEXT("utt-%d"), UtteranceOrdinal);
	}

	CurrentSpeechId = Record.SpeechId;
	CurrentRecord = Record;
	RecordsBySpeechId.Add(Record.SpeechId, Record);

	UE_LOG(LogGodfreySpeech, Log,
		TEXT("[Speech] Speech Generated | SpeechId=%s Ordinal=%d BrainRequestId=%s"),
		*Record.SpeechId,
		UtteranceOrdinal,
		BrainRequestId.IsEmpty() ? TEXT("(none)") : *BrainRequestId);

	return Record.SpeechId;
}

void UGodfreyDiagnosticsSubsystem::MarkStage(const FString& SpeechId, EGodfreyUtteranceStage Stage)
{
	if (SpeechId.IsEmpty())
	{
		return;
	}

	FGodfreyUtteranceTimingRecord* Record = RecordsBySpeechId.Find(SpeechId);
	if (!Record)
	{
		return;
	}

	const double ElapsedMs = (FPlatformTime::Seconds() - Record->T0PlatformSeconds) * 1000.0;
	ApplyStageTimestamp(*Record, Stage, ElapsedMs);

	if (SpeechId == CurrentSpeechId)
	{
		CurrentRecord = *Record;
	}

	const bool bStructured = GetDefault<UUnrealPerformerGodfreySettings>()->bGodfreyStructuredPipelineLogging;
	if (bStructured)
	{
		const FString Line = FString::Printf(
			TEXT("[%s] %s | SpeechId=%s +%.1fms"),
			StageToChannel(Stage),
			StageToLabel(Stage),
			*SpeechId,
			ElapsedMs);

		switch (Stage)
		{
		case EGodfreyUtteranceStage::AceStarted:
		case EGodfreyUtteranceStage::AceComplete:
			UE_LOG(LogGodfreyACE, Log, TEXT("%s"), *Line);
			break;
		case EGodfreyUtteranceStage::AudioQueued:
		case EGodfreyUtteranceStage::AudioPlaybackStarted:
		case EGodfreyUtteranceStage::FirstAudibleSample:
			UE_LOG(LogGodfreyAudio, Log, TEXT("%s"), *Line);
			break;
		case EGodfreyUtteranceStage::BehaviourStarted:
		case EGodfreyUtteranceStage::BehaviourFinished:
			UE_LOG(LogGodfreyBehaviour, Log, TEXT("%s"), *Line);
			break;
		case EGodfreyUtteranceStage::BodyAnimStarted:
		case EGodfreyUtteranceStage::BodyAnimEnded:
			UE_LOG(LogGodfreyAnimation, Log, TEXT("%s"), *Line);
			break;
		default:
			UE_LOG(LogGodfreySpeech, Log, TEXT("%s"), *Line);
			break;
		}
	}

	if (Stage == EGodfreyUtteranceStage::SpeechFinished
		|| Stage == EGodfreyUtteranceStage::AceComplete)
	{
		const float Latency = static_cast<float>(
			Record->AudioPlaybackBeginsMs >= 0.0 ? Record->AudioPlaybackBeginsMs : ElapsedMs);
		SpeechLatencyMs = Latency;
		if (GetDefault<UUnrealPerformerGodfreySettings>()->bGodfreyLogUtteranceTimingMs)
		{
			LogTimingSummary(SpeechId);
		}
	}
}

void UGodfreyDiagnosticsSubsystem::MarkStageForCurrent(EGodfreyUtteranceStage Stage)
{
	if (!CurrentSpeechId.IsEmpty())
	{
		MarkStage(CurrentSpeechId, Stage);
	}
}

void UGodfreyDiagnosticsSubsystem::SetBehaviourStateName(const FString& StateName)
{
	BehaviourStateName = StateName;
}

void UGodfreyDiagnosticsSubsystem::SetCurrentAnimationName(const FString& AnimName)
{
	CurrentAnimationName = AnimName;
}

void UGodfreyDiagnosticsSubsystem::SetQueueLength(int32 Length)
{
	QueueLength = Length;
}

void UGodfreyDiagnosticsSubsystem::SetSpeechLatencyMs(float LatencyMs)
{
	SpeechLatencyMs = LatencyMs;
}

void UGodfreyDiagnosticsSubsystem::LogTimingSummary(const FString& SpeechId) const
{
	const FGodfreyUtteranceTimingRecord* Record = RecordsBySpeechId.Find(SpeechId);
	if (!Record)
	{
		return;
	}

	UE_LOG(LogGodfreyPerfMonitor, Log,
		TEXT("[Performance] TimingMs SpeechId=%s | AudioRecv=%.1f AudioQueued=%.1f AceBegin=%.1f AceDone=%.1f PlayBegin=%.1f FirstAudible=%.1f SpeechDone=%.1f BehaviourStart=%.1f BehaviourEnd=%.1f BodyStart=%.1f BodyEnd=%.1f"),
		*Record->SpeechId,
		Record->AudioReceivedMs,
		Record->AudioQueuedMs,
		Record->AceProcessingBeginsMs,
		Record->AceCompleteMs,
		Record->AudioPlaybackBeginsMs,
		Record->FirstAudibleSampleMs,
		Record->SpeechCompleteMs,
		Record->BehaviourStartedMs,
		Record->BehaviourFinishedMs,
		Record->BodyAnimStartedMs,
		Record->BodyAnimEndedMs);
}

void UGodfreyDiagnosticsSubsystem::ApplyStageTimestamp(FGodfreyUtteranceTimingRecord& Record, EGodfreyUtteranceStage Stage, double ElapsedMs) const
{
	auto SetOnce = [ElapsedMs](double& Field)
	{
		if (Field < 0.0)
		{
			Field = ElapsedMs;
		}
	};

	switch (Stage)
	{
	case EGodfreyUtteranceStage::SpeechGenerated:
		break;
	case EGodfreyUtteranceStage::AudioReady:
		SetOnce(Record.AudioReceivedMs);
		break;
	case EGodfreyUtteranceStage::AudioQueued:
		SetOnce(Record.AudioQueuedMs);
		break;
	case EGodfreyUtteranceStage::AceStarted:
		SetOnce(Record.AceProcessingBeginsMs);
		break;
	case EGodfreyUtteranceStage::AceComplete:
		SetOnce(Record.AceCompleteMs);
		break;
	case EGodfreyUtteranceStage::AudioPlaybackStarted:
		SetOnce(Record.AudioPlaybackBeginsMs);
		break;
	case EGodfreyUtteranceStage::FirstAudibleSample:
		SetOnce(Record.FirstAudibleSampleMs);
		break;
	case EGodfreyUtteranceStage::BehaviourStarted:
		SetOnce(Record.BehaviourStartedMs);
		break;
	case EGodfreyUtteranceStage::SpeechFinished:
		SetOnce(Record.SpeechCompleteMs);
		break;
	case EGodfreyUtteranceStage::BehaviourFinished:
		SetOnce(Record.BehaviourFinishedMs);
		break;
	case EGodfreyUtteranceStage::BodyAnimStarted:
		SetOnce(Record.BodyAnimStartedMs);
		break;
	case EGodfreyUtteranceStage::BodyAnimEnded:
		SetOnce(Record.BodyAnimEndedMs);
		break;
	default:
		break;
	}
}

const TCHAR* UGodfreyDiagnosticsSubsystem::StageToChannel(EGodfreyUtteranceStage Stage)
{
	switch (Stage)
	{
	case EGodfreyUtteranceStage::SpeechGenerated:
	case EGodfreyUtteranceStage::AudioReady:
	case EGodfreyUtteranceStage::SpeechFinished:
		return TEXT("Speech");
	case EGodfreyUtteranceStage::AceStarted:
	case EGodfreyUtteranceStage::AceComplete:
		return TEXT("ACE");
	case EGodfreyUtteranceStage::AudioQueued:
	case EGodfreyUtteranceStage::AudioPlaybackStarted:
	case EGodfreyUtteranceStage::FirstAudibleSample:
		return TEXT("Audio");
	case EGodfreyUtteranceStage::BehaviourStarted:
	case EGodfreyUtteranceStage::BehaviourFinished:
		return TEXT("Behaviour");
	case EGodfreyUtteranceStage::BodyAnimStarted:
	case EGodfreyUtteranceStage::BodyAnimEnded:
		return TEXT("Animation");
	default:
		return TEXT("Performance");
	}
}

const TCHAR* UGodfreyDiagnosticsSubsystem::StageToLabel(EGodfreyUtteranceStage Stage)
{
	switch (Stage)
	{
	case EGodfreyUtteranceStage::SpeechGenerated: return TEXT("Speech Generated");
	case EGodfreyUtteranceStage::AudioReady: return TEXT("Audio Ready");
	case EGodfreyUtteranceStage::AudioQueued: return TEXT("Audio Queued");
	case EGodfreyUtteranceStage::AceStarted: return TEXT("ACE Started");
	case EGodfreyUtteranceStage::AceComplete: return TEXT("ACE Complete");
	case EGodfreyUtteranceStage::AudioPlaybackStarted: return TEXT("Audio Playback Started");
	case EGodfreyUtteranceStage::FirstAudibleSample: return TEXT("First Audible Sample");
	case EGodfreyUtteranceStage::BehaviourStarted: return TEXT("Behaviour Started");
	case EGodfreyUtteranceStage::SpeechFinished: return TEXT("Speech Finished");
	case EGodfreyUtteranceStage::BehaviourFinished: return TEXT("Behaviour Finished");
	case EGodfreyUtteranceStage::BodyAnimStarted: return TEXT("Body Animation Begins");
	case EGodfreyUtteranceStage::BodyAnimEnded: return TEXT("Body Animation Ends");
	default: return TEXT("Unknown");
	}
}
