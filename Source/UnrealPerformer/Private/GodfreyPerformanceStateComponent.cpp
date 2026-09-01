#include "GodfreyPerformanceStateComponent.h"

#include "EngineUtils.h"
#include "GodfreyDiagnostics.h"
#include "GodfreyDirectSpeechComponent.h"
#include "GodfreyPerformanceLog.h"
#include "GodfreyPcmStreamSession.h"
#include "GodfreyVisitorPresenceComponent.h"
#include "GodfreyVoiceInputComponent.h"
#include "UnrealPerformerGodfreySettings.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

namespace
{
static bool GodfreyCueTokenContainsKeyword(const FString& Token, const TCHAR* Keyword)
{
	if (Token.IsEmpty() || !Keyword)
	{
		return false;
	}
	return Token.Contains(Keyword, ESearchCase::IgnoreCase);
}

static bool GodfreyIsNamedActionCueType(const FString& CueType)
{
	const FString T = CueType.TrimStartAndEnd().ToLower();
	return T == TEXT("action") || T == TEXT("performance") || T == TEXT("gesture") || T == TEXT("anim")
		|| T == TEXT("clip");
}

/** Library ids like SpeakingDescribeSize_01 / AS_TwoThumbsUp_01 — must not hit coarse "speak" Contains(). */
static bool GodfreyLooksLikeNamedPerformanceId(const FString& Token)
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

static bool GodfreyCueTokenEqualsAny(const FString& Token, const TCHAR* const* Exacts, const int32 Count)
{
	for (int32 Index = 0; Index < Count; ++Index)
	{
		if (Token.Equals(Exacts[Index], ESearchCase::IgnoreCase))
		{
			return true;
		}
	}
	return false;
}
} // namespace

UGodfreyPerformanceStateComponent::UGodfreyPerformanceStateComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UGodfreyPerformanceStateComponent::BeginPlay()
{
	Super::BeginPlay();
	PerformanceState = EGodfreyPerformanceState::Idle;
	if (const UUnrealPerformerGodfreySettings* Settings = GetDefault<UUnrealPerformerGodfreySettings>())
	{
		DialogEngageSilenceSeconds = Settings->GodfreyDialogEngageSilenceSeconds;
		bEnableDialogEngagementPrompts = Settings->bGodfreyEnableDialogEngagementPrompts;
		DialogEngageMaxUnansweredAttempts = Settings->GodfreyDialogEngageMaxUnansweredAttempts;
		if (!Settings->GodfreyDialogEngagePrompt.IsEmpty())
		{
			DialogEngagePrompt = Settings->GodfreyDialogEngagePrompt;
		}
	}
	if (AActor* const Owner = GetOwner())
	{
		UE_LOG(LogGodfreyPerformance, Log, TEXT("GodfreyPerformer: BeginPlay owner=%s"), *Owner->GetName());
	}
	else
	{
		UE_LOG(LogGodfreyPerformance, Log, TEXT("GodfreyPerformer: BeginPlay (no owner)"));
	}

	if (bEnableExhibitionPresence)
	{
		SetComponentTickEnabled(true);
		if (UWorld* World = GetWorld())
		{
			// Defer so AnimationBridge can bind delegates before OnSeaIdleStarted fires.
			World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateUObject(this,
				&UGodfreyPerformanceStateComponent::EnterSeaIdlePresence));
		}
		else
		{
			EnterSeaIdlePresence();
		}
	}
}

void UGodfreyPerformanceStateComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	SetComponentTickEnabled(false);
	UGodfreyPcmStreamSession::AbortActiveStreamForCharacter(GetOwner(), TEXT("performance state EndPlay"));
	Super::EndPlay(EndPlayReason);
}

void UGodfreyPerformanceStateComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (bEnableExhibitionPresence)
	{
		TickPendingConversationEnd(DeltaTime);
		TickVisitorIdleTimeout(DeltaTime);
		TickDialogEngagement(DeltaTime);
	}
}

void UGodfreyPerformanceStateComponent::NotifyVisitorActivity()
{
	if (const UWorld* World = GetWorld())
	{
		LastVisitorActivityWorldTime = World->GetTimeSeconds();
	}
}

void UGodfreyPerformanceStateComponent::NotifyVisitorSpeechBegan()
{
	NotifyVisitorActivity();
	ResetUnansweredDialogEngages();
	// End silent-period clock as soon as the visitor starts talking (not when STT final arrives).
	LastDialogExchangeWorldTime = -1.0;
	AbortDialogEngagePromptIfInFlight(TEXT("visitor speech_started"));
	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("GodfreyPerformer: visitor speech began — dialog engage timer cleared."));

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!IsValid(Actor))
		{
			continue;
		}
		if (UGodfreyVisitorPresenceComponent* Presence =
			Actor->FindComponentByClass<UGodfreyVisitorPresenceComponent>())
		{
			Presence->NotifyPostFarewellVisitorSpeech();
		}
	}
}

void UGodfreyPerformanceStateComponent::NotifyVisitorSpeechEnded()
{
	NotifyVisitorActivity();
	// Visitor paused — restart silence clock so we wait a full engage window for a final / next line.
	if (PerformanceState == EGodfreyPerformanceState::Speaking
		|| PerformanceState == EGodfreyPerformanceState::Thinking
		|| bAwaitingBrainReply)
	{
		LastDialogExchangeWorldTime = -1.0;
		return;
	}
	MarkDialogExchange();
	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("GodfreyPerformer: visitor speech ended — dialog engage timer restarted (%.1fs)."),
		DialogEngageSilenceSeconds);
}

void UGodfreyPerformanceStateComponent::NotifyVisitorSpoke()
{
	NotifyVisitorActivity();
	ResetUnansweredDialogEngages();
	// Anyone speaking ends the silent period; clock restarts only when Speak goes green again.
	LastDialogExchangeWorldTime = -1.0;
}

void UGodfreyPerformanceStateComponent::NotifyListeningWindowOpened()
{
	// Start of a silent listen period (Speak green).
	MarkDialogExchange();
}

void UGodfreyPerformanceStateComponent::MarkDialogExchange()
{
	if (const UWorld* World = GetWorld())
	{
		LastDialogExchangeWorldTime = World->GetTimeSeconds();
	}
}

void UGodfreyPerformanceStateComponent::SetExhibitionPresence(const EGodfreyExhibitionPresence NewPresence)
{
	if (NewPresence == ExhibitionPresence)
	{
		return;
	}
	const EGodfreyExhibitionPresence Previous = ExhibitionPresence;
	ExhibitionPresence = NewPresence;
	UE_LOG(LogGodfreyPerformance, Log, TEXT("GodfreyPerformer: exhibition presence %d -> %d"),
		static_cast<int32>(Previous), static_cast<int32>(NewPresence));
	OnExhibitionPresenceChanged.Broadcast(NewPresence, Previous);
}

void UGodfreyPerformanceStateComponent::EnterSeaIdlePresence()
{
	bHasEngagedVisitor = false;
	PendingPostEngageState = EGodfreyPerformanceState::Idle;
	LastDialogExchangeWorldTime = -1.0;
	bDialogEngagePromptInFlight = false;
	ResetUnansweredDialogEngages();
	SetExhibitionPresence(EGodfreyExhibitionPresence::SeaIdle);
	TrySetPerformanceState(EGodfreyPerformanceState::Idle);
	OnSeaIdleStarted.Broadcast();
	NotifyVisitorActivity();
}

bool UGodfreyPerformanceStateComponent::IsInDialog() const
{
	if (!bEnableExhibitionPresence)
	{
		// Without presence director, treat Listening/Thinking/Speaking as in-dialog.
		return PerformanceState == EGodfreyPerformanceState::Listening
			|| PerformanceState == EGodfreyPerformanceState::Thinking
			|| PerformanceState == EGodfreyPerformanceState::Speaking
			|| PerformanceState == EGodfreyPerformanceState::Emphasising
			|| PerformanceState == EGodfreyPerformanceState::Serious
			|| PerformanceState == EGodfreyPerformanceState::Amused;
	}
	// In dialog from first visitor engage until farewell begins (SeaIdle / Farewell = out).
	return ExhibitionPresence == EGodfreyExhibitionPresence::Engaging
		|| ExhibitionPresence == EGodfreyExhibitionPresence::Conversing;
}

void UGodfreyPerformanceStateComponent::NotifyVisitorEngaged()
{
	if (!bEnableExhibitionPresence)
	{
		BeginListening();
		return;
	}

	NotifyVisitorActivity();
	ResetUnansweredDialogEngages();

	if (ExhibitionPresence == EGodfreyExhibitionPresence::Farewell)
	{
		// Ignore engage during farewell; wait for sea idle.
		return;
	}

	if (bHasEngagedVisitor && ExhibitionPresence == EGodfreyExhibitionPresence::Conversing)
	{
		BeginListening();
		return;
	}

	if (bHasEngagedVisitor && ExhibitionPresence == EGodfreyExhibitionPresence::Engaging)
	{
		return;
	}

	bHasEngagedVisitor = true;
	SetExhibitionPresence(EGodfreyExhibitionPresence::Engaging);
	OnEngageSequenceStarted.Broadcast();
	UE_LOG(LogGodfreyPerformance, Log, TEXT("GodfreyPerformer: visitor engaged — greet sequence then listening."));
}

void UGodfreyPerformanceStateComponent::BeginFarewell()
{
	if (!bEnableExhibitionPresence)
	{
		ReturnToIdle();
		return;
	}

	if (ExhibitionPresence == EGodfreyExhibitionPresence::Farewell
		|| ExhibitionPresence == EGodfreyExhibitionPresence::SeaIdle)
	{
		return;
	}

	UE_LOG(LogGodfreyPerformance, Log, TEXT("GodfreyPerformer: BeginFarewell"));
	bConversationEndRequested = false;
	ConversationEndReason.Reset();
	ConversationEndWaitStartWorldTime = -1.0;
	SetExhibitionPresence(EGodfreyExhibitionPresence::Farewell);
	bHasEngagedVisitor = false;
	PendingPostEngageState = EGodfreyPerformanceState::Idle;
	OnFarewellSequenceStarted.Broadcast();
}

void UGodfreyPerformanceStateComponent::RequestConversationEnd(const FString& Reason)
{
	if (!bEnableExhibitionPresence)
	{
		UE_LOG(LogGodfreyPerformance, Log,
			TEXT("GodfreyPerformer: conversation end requested (%s) without presence director — returning to idle."),
			*Reason);
		ReturnToIdle();
		return;
	}

	if (ExhibitionPresence == EGodfreyExhibitionPresence::Farewell
		|| ExhibitionPresence == EGodfreyExhibitionPresence::SeaIdle)
	{
		UE_LOG(LogGodfreyPerformance, Verbose,
			TEXT("GodfreyPerformer: conversation end requested (%s) but already out of dialog — ignored."), *Reason);
		return;
	}

	if (bConversationEndRequested)
	{
		UE_LOG(LogGodfreyPerformance, Verbose,
			TEXT("GodfreyPerformer: conversation end already pending (%s); new reason=%s"), *ConversationEndReason, *Reason);
		return;
	}

	bConversationEndRequested = true;
	ConversationEndReason = Reason;
	if (const UWorld* World = GetWorld())
	{
		ConversationEndWaitStartWorldTime = World->GetTimeSeconds();
	}
	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("GodfreyPerformer: conversation end latched (%s) — farewell after this reply finishes speaking."), *Reason);
}

void UGodfreyPerformanceStateComponent::CancelPendingConversationEnd(const FString& Reason)
{
	if (!bConversationEndRequested)
	{
		return;
	}
	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("GodfreyPerformer: conversation end cancelled (%s) — visitor is still here."), *Reason);
	bConversationEndRequested = false;
	ConversationEndReason.Reset();
	ConversationEndWaitStartWorldTime = -1.0;
}

void UGodfreyPerformanceStateComponent::RunPendingConversationEnd(const TCHAR* Trigger)
{
	if (!bConversationEndRequested)
	{
		return;
	}
	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("GodfreyPerformer: running latched conversation end (%s) via %s."), *ConversationEndReason, Trigger);
	BeginFarewell();
}

void UGodfreyPerformanceStateComponent::TickPendingConversationEnd(float /*DeltaTime*/)
{
	if (!bConversationEndRequested)
	{
		return;
	}
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// Speech in progress: hold the fallback open so a long goodbye reply plays out in full.
	if (PerformanceState == EGodfreyPerformanceState::Speaking || bAwaitingBrainReply)
	{
		ConversationEndWaitStartWorldTime = World->GetTimeSeconds();
		return;
	}

	if (ConversationEndWaitStartWorldTime < 0.0)
	{
		ConversationEndWaitStartWorldTime = World->GetTimeSeconds();
		return;
	}

	if ((World->GetTimeSeconds() - ConversationEndWaitStartWorldTime) >= static_cast<double>(ConversationEndFallbackSeconds))
	{
		UE_LOG(LogGodfreyPerformance, Warning,
			TEXT("GodfreyPerformer: conversation end fallback after %.0fs of silence (no utterance-ended hook)."),
			ConversationEndFallbackSeconds);
		RunPendingConversationEnd(TEXT("silence fallback"));
	}
}

void UGodfreyPerformanceStateComponent::NotifyEngageSequenceFinished()
{
	if (ExhibitionPresence != EGodfreyExhibitionPresence::Engaging)
	{
		UE_LOG(LogGodfreyPerformance, Log,
			TEXT("GodfreyPerformer: NotifyEngageSequenceFinished ignored (presence=%d, not Engaging)."),
			static_cast<int32>(ExhibitionPresence));
		return;
	}
	NotifyVisitorActivity();
	SetExhibitionPresence(EGodfreyExhibitionPresence::Conversing);

	// Pending Speaking means BeginSpeaking was deferred while Engaging/SeaIdle engage ran.
	// That is the normal path for occasion / queue speech that starts engage (especially when
	// EngageTurn/Greet are skipped and this finishes in the same stack as BeginSpeaking).
	// Do NOT treat "PerformanceState != Speaking" as stale — utterance-end already rewrites
	// PendingPostEngageState away from Speaking in NotifyUtteranceEnded / EndSpeaking.
	const bool bPendingSpeaking =
		PendingPostEngageState == EGodfreyPerformanceState::Speaking;
	if (!bPendingSpeaking && PerformanceState != EGodfreyPerformanceState::Speaking)
	{
		EnterListening();
	}
	ApplyPendingPostEngageState();
	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("GodfreyPerformer: engage finished — Conversing (pendingSpeak=%d alreadySpeaking=%d)."),
		bPendingSpeaking ? 1 : 0,
		PerformanceState == EGodfreyPerformanceState::Speaking ? 1 : 0);
}

void UGodfreyPerformanceStateComponent::NotifyFarewellSequenceFinished()
{
	EnterSeaIdlePresence();
	UE_LOG(LogGodfreyPerformance, Log, TEXT("GodfreyPerformer: farewell finished — SeaIdle."));
}

void UGodfreyPerformanceStateComponent::TickVisitorIdleTimeout(float /*DeltaTime*/)
{
	if (!bHasEngagedVisitor || ExhibitionPresence != EGodfreyExhibitionPresence::Conversing)
	{
		return;
	}
	if (PerformanceState == EGodfreyPerformanceState::Speaking)
	{
		return;
	}
	const UWorld* World = GetWorld();
	if (!World || LastVisitorActivityWorldTime < 0.0)
	{
		return;
	}
	if ((World->GetTimeSeconds() - LastVisitorActivityWorldTime) >= static_cast<double>(VisitorIdleTimeoutSeconds))
	{
		UE_LOG(LogGodfreyPerformance, Log,
			TEXT("GodfreyPerformer: visitor idle timeout (%.0fs) — farewell."), VisitorIdleTimeoutSeconds);
		BeginFarewell();
	}
}

void UGodfreyPerformanceStateComponent::TickDialogEngagement(float /*DeltaTime*/)
{
	if (!bEnableDialogEngagementPrompts || DialogEngageSilenceSeconds <= KINDA_SMALL_NUMBER)
	{
		return;
	}
	if (bDialogEngagePromptInFlight)
	{
		return;
	}
	if (!bHasEngagedVisitor || ExhibitionPresence != EGodfreyExhibitionPresence::Conversing)
	{
		return;
	}
	if (bConversationEndRequested)
	{
		return;
	}
	if (!IsVisitorVisiblyPresent())
	{
		return;
	}
	if (IsGodfreyBusyForDialogEngage())
	{
		return;
	}
	if (!IsVisitorListenWindowOpen())
	{
		return;
	}

	const UWorld* World = GetWorld();
	if (!World || LastDialogExchangeWorldTime < 0.0)
	{
		return;
	}

	const double Silence = World->GetTimeSeconds() - LastDialogExchangeWorldTime;
	if (Silence < static_cast<double>(DialogEngageSilenceSeconds))
	{
		return;
	}

	if (DialogEngageMaxUnansweredAttempts > 0
		&& DialogEngageUnansweredCount >= DialogEngageMaxUnansweredAttempts)
	{
		AssumeVisitorLeftAfterUnansweredEngages();
		return;
	}

	TryFireDialogEngagementPrompt();
}

bool UGodfreyPerformanceStateComponent::IsVisitorListenWindowOpen() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!IsValid(Actor))
		{
			continue;
		}
		if (const UGodfreyVoiceInputComponent* Voice = Actor->FindComponentByClass<UGodfreyVoiceInputComponent>())
		{
			// Do not steal the turn while the visitor is mid-utterance (STT speech_started)
			// or while a final transcript is still expected after speech_stopped.
			if (Voice->IsVisitorSpeechInProgress() || Voice->IsAwaitingVisitorTranscript())
			{
				return false;
			}
			return Voice->CanVisitorSpeak();
		}
	}
	// No game mic — allow engagement on dialog silence alone (keyboard / queue path).
	return true;
}

bool UGodfreyPerformanceStateComponent::IsVisitorVisiblyPresent() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return true;
	}

	bool bFoundPresence = false;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!IsValid(Actor))
		{
			continue;
		}
		if (const UGodfreyVisitorPresenceComponent* Presence =
			Actor->FindComponentByClass<UGodfreyVisitorPresenceComponent>())
		{
			bFoundPresence = true;
			const EGodfreyVisitorSenseState Sense = Presence->GetVisitorSenseState();
			if (Sense == EGodfreyVisitorSenseState::Present
				|| Sense == EGodfreyVisitorSenseState::Leaving)
			{
				return true;
			}
		}
	}

	// No webcam presence component → allow engagement (dev / non-presence setups).
	return !bFoundPresence;
}

bool UGodfreyPerformanceStateComponent::IsGodfreyBusyForDialogEngage() const
{
	if (PerformanceState == EGodfreyPerformanceState::Speaking
		|| PerformanceState == EGodfreyPerformanceState::Thinking
		|| bAwaitingBrainReply)
	{
		return true;
	}
	if (UGodfreyPcmStreamSession::IsCharacterAudiblePlaybackActive(GetOwner()))
	{
		return true;
	}
	if (UGodfreyDirectSpeechComponent* Speech = FindDirectSpeech())
	{
		if (Speech->IsStreaming())
		{
			return true;
		}
	}
	return false;
}

bool UGodfreyPerformanceStateComponent::TryFireDialogEngagementPrompt()
{
	if (!IsVisitorListenWindowOpen())
	{
		UE_LOG(LogGodfreyPerformance, Log,
			TEXT("GodfreyPerformer: dialog engagement skipped — incoming visitor speech."));
		return false;
	}

	const FString Prompt = DialogEngagePrompt.TrimStartAndEnd();
	if (Prompt.IsEmpty())
	{
		return false;
	}

	UGodfreyDirectSpeechComponent* Speech = FindDirectSpeech();
	if (!Speech || Speech->IsStreaming())
	{
		return false;
	}

	// Reset clock first so a rejected AskGodfrey cannot spam every tick,
	// and so we do not treat "visitor spoke N seconds ago" as silence after this turn.
	LastDialogExchangeWorldTime = -1.0;
	NotifyVisitorActivity();

	const bool bOk = Speech->AskGodfrey(Prompt);
	if (bOk)
	{
		++DialogEngageUnansweredCount;
		bDialogEngagePromptInFlight = true;
	}
	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("GodfreyPerformer: dialog engagement AskGodfrey ok=%d unanswered=%d/%d (need %.1fs Speak-green silence)"),
		bOk ? 1 : 0,
		DialogEngageUnansweredCount,
		DialogEngageMaxUnansweredAttempts,
		DialogEngageSilenceSeconds);
	if (!bOk)
	{
		// Allow another attempt after a fresh listen window opens.
		LastDialogExchangeWorldTime = -1.0;
	}
	return bOk;
}

void UGodfreyPerformanceStateComponent::ResetUnansweredDialogEngages()
{
	if (DialogEngageUnansweredCount <= 0)
	{
		return;
	}
	UE_LOG(LogGodfreyPerformance, Verbose,
		TEXT("GodfreyPerformer: unanswered dialog engages reset (was %d)."), DialogEngageUnansweredCount);
	DialogEngageUnansweredCount = 0;
}

UGodfreyDirectSpeechComponent* UGodfreyPerformanceStateComponent::FindDirectSpeech() const
{
	if (AActor* Owner = GetOwner())
	{
		if (UGodfreyDirectSpeechComponent* Speech = Owner->FindComponentByClass<UGodfreyDirectSpeechComponent>())
		{
			return Speech;
		}
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!IsValid(Actor))
		{
			continue;
		}
		if (UGodfreyDirectSpeechComponent* Found = Actor->FindComponentByClass<UGodfreyDirectSpeechComponent>())
		{
			return Found;
		}
	}
	return nullptr;
}

void UGodfreyPerformanceStateComponent::AbortDialogEngagePromptIfInFlight(const TCHAR* Reason)
{
	if (!bDialogEngagePromptInFlight)
	{
		return;
	}
	bDialogEngagePromptInFlight = false;
	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("GodfreyPerformer: aborting dialog engagement prompt (%s)."), Reason ? Reason : TEXT("unknown"));
	if (UGodfreyDirectSpeechComponent* Speech = FindDirectSpeech())
	{
		Speech->AbortCurrentStream(Reason ? FString(Reason) : FString(TEXT("dialog engage abort")));
	}
}

void UGodfreyPerformanceStateComponent::NotifyPresenceEncounterAbandoned()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!IsValid(Actor))
		{
			continue;
		}
		if (UGodfreyVisitorPresenceComponent* Presence =
			Actor->FindComponentByClass<UGodfreyVisitorPresenceComponent>())
		{
			Presence->NotifyEncounterAbandonedWhileOccupied();
		}
	}
}

void UGodfreyPerformanceStateComponent::AssumeVisitorLeftAfterUnansweredEngages()
{
	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("GodfreyPerformer: %d unanswered engagement prompt(s) — assuming visitor left, waiting for next."),
		DialogEngageUnansweredCount);
	LastDialogExchangeWorldTime = -1.0;
	NotifyPresenceEncounterAbandoned();
	ResetUnansweredDialogEngages();
	BeginFarewell();
}

bool UGodfreyPerformanceStateComponent::LooksLikeFarewellCue(const FString& CueType, const FString& CueValue) const
{
	const FString T = NormalizeCueToken(CueType);
	const FString V = NormalizeCueToken(CueValue);
	if (T == TEXT("farewell") || V == TEXT("farewell") || V == TEXT("goodbye") || V == TEXT("good bye"))
	{
		return true;
	}
	if (GodfreyCueTokenContainsKeyword(V, TEXT("farewell")) || GodfreyCueTokenContainsKeyword(V, TEXT("goodbye")))
	{
		return true;
	}
	if (GodfreyCueTokenContainsKeyword(V, TEXT("farewellwave")) || V.Contains(TEXT("farewell"), ESearchCase::IgnoreCase))
	{
		return true;
	}
	return false;
}

void UGodfreyPerformanceStateComponent::BeginListening()
{
	if (bEnableExhibitionPresence && ExhibitionPresence == EGodfreyExhibitionPresence::SeaIdle)
	{
		SetPendingPostEngageState(EGodfreyPerformanceState::Listening);
		NotifyVisitorEngaged();
		return;
	}
	if (bEnableExhibitionPresence && ExhibitionPresence == EGodfreyExhibitionPresence::Engaging)
	{
		SetPendingPostEngageState(EGodfreyPerformanceState::Listening);
		NotifyVisitorActivity();
		return;
	}
	if (bEnableExhibitionPresence && ExhibitionPresence == EGodfreyExhibitionPresence::Farewell)
	{
		return;
	}
	NotifyVisitorActivity();
	if (bEnableExhibitionPresence && bHasEngagedVisitor)
	{
		SetExhibitionPresence(EGodfreyExhibitionPresence::Conversing);
	}
	EnterListening();
}

void UGodfreyPerformanceStateComponent::BeginThinking()
{
	if (bEnableExhibitionPresence && ExhibitionPresence == EGodfreyExhibitionPresence::SeaIdle)
	{
		SetPendingPostEngageState(EGodfreyPerformanceState::Thinking);
		NotifyVisitorEngaged();
		return;
	}
	if (bEnableExhibitionPresence && ExhibitionPresence == EGodfreyExhibitionPresence::Engaging)
	{
		SetPendingPostEngageState(EGodfreyPerformanceState::Thinking);
		NotifyVisitorActivity();
		return;
	}
	if (bEnableExhibitionPresence && ExhibitionPresence == EGodfreyExhibitionPresence::Farewell)
	{
		return;
	}
	NotifyVisitorActivity();
	if (bEnableExhibitionPresence && bHasEngagedVisitor)
	{
		SetExhibitionPresence(EGodfreyExhibitionPresence::Conversing);
	}
	EnterThinking();
}

void UGodfreyPerformanceStateComponent::BeginSpeaking()
{
	// Drop awaiting-brain without rebroadcasting Listening — speaking preempts immediately.
	if (bAwaitingBrainReply)
	{
		UE_LOG(LogGodfreyPerformance, Log,
			TEXT("GodfreyPerformer: BeginSpeaking clears awaiting_reply requestId=%s"), *AwaitingBrainRequestId);
		bAwaitingBrainReply = false;
		AwaitingBrainRequestId.Empty();
	}
	if (bEnableExhibitionPresence && ExhibitionPresence == EGodfreyExhibitionPresence::SeaIdle)
	{
		SetPendingPostEngageState(EGodfreyPerformanceState::Speaking);
		NotifyVisitorEngaged();
		// EngageTurn/Greet are often skipped — engage then finishes in this same call stack.
		// If we are already Conversing and still not Speaking, enter Speaking here so body
		// never stays on the listening deck for the whole utterance (occasion/queue path).
		if (ExhibitionPresence == EGodfreyExhibitionPresence::Conversing
			&& PerformanceState != EGodfreyPerformanceState::Speaking)
		{
			PendingPostEngageState = EGodfreyPerformanceState::Idle;
			UE_LOG(LogGodfreyPerformance, Log,
				TEXT("GodfreyPerformer: BeginSpeaking — sync engage from SeaIdle; EnterSpeaking now."));
			EnterSpeaking();
		}
		return;
	}
	if (bEnableExhibitionPresence && ExhibitionPresence == EGodfreyExhibitionPresence::Engaging)
	{
		SetPendingPostEngageState(EGodfreyPerformanceState::Speaking);
		NotifyVisitorActivity();
		return;
	}
	if (bEnableExhibitionPresence && ExhibitionPresence == EGodfreyExhibitionPresence::Farewell)
	{
		return;
	}
	NotifyVisitorActivity();
	// Keep Engaging during greet chain; still enter Speaking so ACE/body speaking idle can run.
	if (bEnableExhibitionPresence && bHasEngagedVisitor
		&& ExhibitionPresence != EGodfreyExhibitionPresence::Engaging)
	{
		SetExhibitionPresence(EGodfreyExhibitionPresence::Conversing);
	}
	EnterSpeaking();
}

void UGodfreyPerformanceStateComponent::EndSpeaking()
{
	// Always clear deferred speak-after-engage — utterance audio is done even if we never
	// EnterSpeaking'd yet (BeginSpeaking while Engaging only sets PendingPostEngageState).
	if (PendingPostEngageState == EGodfreyPerformanceState::Speaking)
	{
		PendingPostEngageState = (bEnableExhibitionPresence && bReturnToListeningWhileEngaged && bHasEngagedVisitor)
			? EGodfreyPerformanceState::Listening
			: EGodfreyPerformanceState::Idle;
		UE_LOG(LogGodfreyPerformance, Log,
			TEXT("GodfreyPerformer: EndSpeaking cleared pending Speaking -> %d (avoid speak-after-greet race)"),
			static_cast<int32>(PendingPostEngageState));
	}

	if (PerformanceState != EGodfreyPerformanceState::Speaking)
	{
		UE_LOG(LogGodfreyPerformance, Verbose, TEXT("GodfreyPerformer: EndSpeaking ignored (not Speaking)."));
		return;
	}
	NotifyVisitorActivity();
	if (bEnableExhibitionPresence && bReturnToListeningWhileEngaged && bHasEngagedVisitor
		&& (ExhibitionPresence == EGodfreyExhibitionPresence::Conversing
			|| ExhibitionPresence == EGodfreyExhibitionPresence::Engaging))
	{
		UE_LOG(LogGodfreyPerformance, Log, TEXT("GodfreyPerformer: EndSpeaking -> Listening (engaged)"));
		TrySetPerformanceState(EGodfreyPerformanceState::Listening);
		return;
	}
	UE_LOG(LogGodfreyPerformance, Log, TEXT("GodfreyPerformer: EndSpeaking -> Idle"));
	TrySetPerformanceState(EGodfreyPerformanceState::Idle);
}

void UGodfreyPerformanceStateComponent::ReturnToIdle()
{
	if (bEnableExhibitionPresence && bReturnToListeningWhileEngaged && bHasEngagedVisitor
		&& ExhibitionPresence == EGodfreyExhibitionPresence::Conversing
		&& PerformanceState != EGodfreyPerformanceState::Speaking)
	{
		NotifyVisitorActivity();
		EnterListening();
		return;
	}
	EnterIdle();
}

void UGodfreyPerformanceStateComponent::TriggerEmphasis()
{
	if (PerformanceState == EGodfreyPerformanceState::Emphasising)
	{
		UE_LOG(LogGodfreyPerformance, Verbose, TEXT("GodfreyPerformer: TriggerEmphasis retrigger (already Emphasising)."));
		OnEmphasisTriggered.Broadcast();
		return;
	}
	EnterEmphasising();
}

void UGodfreyPerformanceStateComponent::TriggerAmused()
{
	if (PerformanceState == EGodfreyPerformanceState::Amused)
	{
		UE_LOG(LogGodfreyPerformance, Verbose, TEXT("GodfreyPerformer: TriggerAmused retrigger."));
		OnAmusedTriggered.Broadcast();
		return;
	}
	EnterAmused();
}

void UGodfreyPerformanceStateComponent::TriggerSerious()
{
	if (PerformanceState == EGodfreyPerformanceState::Serious)
	{
		UE_LOG(LogGodfreyPerformance, Verbose, TEXT("GodfreyPerformer: TriggerSerious retrigger."));
		OnSeriousTriggered.Broadcast();
		return;
	}
	EnterSerious();
}

bool UGodfreyPerformanceStateComponent::TrySetPerformanceState(const EGodfreyPerformanceState NewState)
{
	if (NewState == PerformanceState)
	{
		return false;
	}
	ApplyPerformanceState(NewState);
	return true;
}

void UGodfreyPerformanceStateComponent::EnterIdle()
{
	TrySetPerformanceState(EGodfreyPerformanceState::Idle);
}

void UGodfreyPerformanceStateComponent::EnterListening()
{
	TrySetPerformanceState(EGodfreyPerformanceState::Listening);
}

void UGodfreyPerformanceStateComponent::EnterThinking()
{
	TrySetPerformanceState(EGodfreyPerformanceState::Thinking);
}

void UGodfreyPerformanceStateComponent::EnterSpeaking()
{
	TrySetPerformanceState(EGodfreyPerformanceState::Speaking);
}

void UGodfreyPerformanceStateComponent::EnterEmphasising()
{
	TrySetPerformanceState(EGodfreyPerformanceState::Emphasising);
}

void UGodfreyPerformanceStateComponent::EnterSerious()
{
	TrySetPerformanceState(EGodfreyPerformanceState::Serious);
}

void UGodfreyPerformanceStateComponent::EnterAmused()
{
	TrySetPerformanceState(EGodfreyPerformanceState::Amused);
}

void UGodfreyPerformanceStateComponent::ResetToIdle()
{
	const EGodfreyPerformanceState Previous = PerformanceState;
	if (Previous == EGodfreyPerformanceState::Idle)
	{
		return;
	}
	ApplyPerformanceState(EGodfreyPerformanceState::Idle);
}

void UGodfreyPerformanceStateComponent::NotifyUtteranceStarted()
{
	UE_LOG(LogGodfreyPerformance, Log, TEXT("GodfreyPerformer: NotifyUtteranceStarted"));
	if (UGodfreyDiagnosticsSubsystem* Diag = UGodfreyDiagnosticsSubsystem::Get(this))
	{
		Diag->MarkStageForCurrent(EGodfreyUtteranceStage::BehaviourStarted);
		Diag->SetBehaviourStateName(TEXT("Speaking"));
	}
	OnGodfreyUtteranceStarted.Broadcast();

	// R10: do not accumulate engage-silence while Godfrey is speaking — wait for Speak green.
	LastDialogExchangeWorldTime = -1.0;

	if (bAutoSpeakingStateFromUtterance)
	{
		UE_LOG(LogGodfreyPerformance, Log, TEXT("GodfreyPerformer: auto utterance -> BeginSpeaking"));
		BeginSpeaking();
	}
}

void UGodfreyPerformanceStateComponent::NotifyUtteranceEnded()
{
	UE_LOG(LogGodfreyPerformance, Log, TEXT("GodfreyPerformer: NotifyUtteranceEnded"));
	bDialogEngagePromptInFlight = false;
	if (UGodfreyDiagnosticsSubsystem* Diag = UGodfreyDiagnosticsSubsystem::Get(this))
	{
		Diag->MarkStageForCurrent(EGodfreyUtteranceStage::BehaviourFinished);
		Diag->SetBehaviourStateName(TEXT("Idle"));
	}
	OnGodfreyUtteranceEnded.Broadcast();

	// Clear deferred Speaking before EndSpeaking so a late engage-greet finish cannot
	// ApplyPendingPostEngageState → EnterSpeaking after audio is already done.
	if (PendingPostEngageState == EGodfreyPerformanceState::Speaking)
	{
		PendingPostEngageState = (bEnableExhibitionPresence && bReturnToListeningWhileEngaged && bHasEngagedVisitor)
			? EGodfreyPerformanceState::Listening
			: EGodfreyPerformanceState::Idle;
		UE_LOG(LogGodfreyPerformance, Log,
			TEXT("GodfreyPerformer: utterance ended — cleared pending Speaking (engage still in progress)."));
	}

	if (bAutoSpeakingStateFromUtterance)
	{
		if (PerformanceState == EGodfreyPerformanceState::Speaking)
		{
			UE_LOG(LogGodfreyPerformance, Log, TEXT("GodfreyPerformer: auto utterance -> EndSpeaking"));
			EndSpeaking();
		}
		else if (PerformanceState == EGodfreyPerformanceState::Thinking)
		{
			// ACE never started (empty/silent TTS, never-started fail) — leave Thinking or mic stays Wait forever.
			UE_LOG(LogGodfreyPerformance, Log,
				TEXT("GodfreyPerformer: utterance ended while Thinking (no audible Speaking) — Listening."));
			if (bEnableExhibitionPresence && bReturnToListeningWhileEngaged && bHasEngagedVisitor
				&& (ExhibitionPresence == EGodfreyExhibitionPresence::Conversing
					|| ExhibitionPresence == EGodfreyExhibitionPresence::Engaging))
			{
				TrySetPerformanceState(EGodfreyPerformanceState::Listening);
			}
			else
			{
				TrySetPerformanceState(EGodfreyPerformanceState::Idle);
			}
		}
		else
		{
			UE_LOG(LogGodfreyPerformance, Log, TEXT("GodfreyPerformer: auto utterance -> EndSpeaking"));
			EndSpeaking();
		}
	}

	// Keep engage clock invalid until VoiceInput NotifyListeningWindowOpened (Speak green).
	LastDialogExchangeWorldTime = -1.0;

	// Goodbye reply has now been heard in full — safe to wave and return to sea (R12).
	RunPendingConversationEnd(TEXT("utterance ended"));
}

void UGodfreyPerformanceStateComponent::NotifyPerformanceCue(const FString& CueType, const FString& CueValue,
	const FString& RawCue)
{
	UE_LOG(LogGodfreyPerformance, Log, TEXT("GodfreyPerformer: cue type=\"%s\" value=\"%s\" raw_len=%d"), *CueType,
		*CueValue, RawCue.Len());
	OnPerformanceCueReceived.Broadcast(CueType, CueValue, RawCue);

	if (LooksLikeFarewellCue(CueType, CueValue))
	{
		// Cues arrive with the queued reply, before its audio streams — wait for the goodbye line to be spoken.
		RequestConversationEnd(FString::Printf(TEXT("brain cue %s=%s"), *CueType, *CueValue));
		return;
	}

	if (bRoutePerformanceCuesToStates)
	{
		const bool bRouted = TryConsumePerformanceCueForRouting(CueType, CueValue);
		if (!bRouted)
		{
			UE_LOG(LogGodfreyPerformance, Verbose,
				TEXT("GodfreyPerformer: cue not matched by built-in routing; handle in Blueprint from OnPerformanceCueReceived."));
		}
	}
}

void UGodfreyPerformanceStateComponent::NotifyNamedPerformanceAction(const FString& ActionId)
{
	NotifyPerformanceCue(TEXT("action"), ActionId, ActionId);
}

void UGodfreyPerformanceStateComponent::NotifyReplyIncoming(const FString& RequestId)
{
	const FString TrimmedId = RequestId.TrimStartAndEnd();
	if (TrimmedId.IsEmpty())
	{
		UE_LOG(LogGodfreyPerformance, Warning, TEXT("GodfreyPerformer: NotifyReplyIncoming ignored (empty requestId)."));
		return;
	}
	if (bAwaitingBrainReply && AwaitingBrainRequestId == TrimmedId)
	{
		UE_LOG(LogGodfreyPerformance, Verbose,
			TEXT("GodfreyPerformer: NotifyReplyIncoming deduped requestId=%s"), *TrimmedId);
		return;
	}

	// A new question means the visitor stayed — drop any goodbye latched from the previous reply.
	CancelPendingConversationEnd(FString::Printf(TEXT("new visitor question %s"), *TrimmedId));

	bAwaitingBrainReply = true;
	AwaitingBrainRequestId = TrimmedId;
	NotifyVisitorActivity();
	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("GodfreyPerformer: NotifyReplyIncoming awaiting_reply requestId=%s"), *TrimmedId);

	if (PerformanceState == EGodfreyPerformanceState::Listening)
	{
		// Already Listening (visitor-await IdleBreathing) — re-fire so bridge plays ListeningEnter.
		OnListeningStarted.Broadcast();
		return;
	}
	BeginListening();
}

void UGodfreyPerformanceStateComponent::ClearReplyIncoming(const bool bRefreshVisitorAwaitListening)
{
	if (!bAwaitingBrainReply)
	{
		return;
	}
	UE_LOG(LogGodfreyPerformance, Log,
		TEXT("GodfreyPerformer: ClearReplyIncoming (was requestId=%s refresh=%d)"),
		*AwaitingBrainRequestId, bRefreshVisitorAwaitListening ? 1 : 0);
	bAwaitingBrainReply = false;
	AwaitingBrainRequestId.Empty();

	// Only refresh visitor-await hold when still Listening (not when speaking is about to start).
	if (bRefreshVisitorAwaitListening
		&& PerformanceState == EGodfreyPerformanceState::Listening
		&& ExhibitionPresence == EGodfreyExhibitionPresence::Conversing)
	{
		OnListeningStarted.Broadcast();
	}
}

FString UGodfreyPerformanceStateComponent::NormalizeCueToken(const FString& In)
{
	return In.TrimStartAndEnd().ToLower();
}

bool UGodfreyPerformanceStateComponent::TryConsumePerformanceCueForRouting(const FString& CueType,
	const FString& CueValue)
{
	const FString T = NormalizeCueToken(CueType);
	const FString V = NormalizeCueToken(CueValue);

	// Named library actions are handled by UGodfreyPerformerAnimationBridgeComponent.
	if (GodfreyIsNamedActionCueType(T) || GodfreyLooksLikeNamedPerformanceId(CueValue)
		|| (CueValue.IsEmpty() && GodfreyLooksLikeNamedPerformanceId(CueType)))
	{
		UE_LOG(LogGodfreyPerformance, Log,
			TEXT("GodfreyPerformer: cue deferred to named-action library (type=\"%s\" value=\"%s\")"), *T, *V);
		return false;
	}

	const bool bTypeIsState = T == TEXT("state") || T == TEXT("behaviour") || T == TEXT("behavior") || T == TEXT("mood")
		|| T == TEXT("performer");
	static const TCHAR* const KnownCoarseTypes[] = {
		TEXT("emphas"), TEXT("emphasis"), TEXT("stress"), TEXT("beat"), TEXT("punch"), TEXT("serious"), TEXT("stern"),
		TEXT("amus"), TEXT("amused"), TEXT("humor"), TEXT("laugh"), TEXT("listen"), TEXT("listening"), TEXT("think"),
		TEXT("thinking"), TEXT("speak"), TEXT("speaking"), TEXT("talk"), TEXT("idle"), TEXT("neutral"), TEXT("farewell"),
		TEXT("goodbye")};
	if (bPreferExplicitStateCueType && !T.IsEmpty() && !bTypeIsState
		&& !GodfreyCueTokenEqualsAny(T, KnownCoarseTypes, UE_ARRAY_COUNT(KnownCoarseTypes)))
	{
		// Unknown non-state type — leave for Blueprint / bridge.
		return false;
	}

	auto RouteCoarseValue = [this](const FString& Token) -> bool
	{
		if (Token.IsEmpty())
		{
			return false;
		}

		static const TCHAR* const EmphasisTokens[] = {
			TEXT("emphasis"), TEXT("emphasise"), TEXT("emphasize"), TEXT("stress"), TEXT("beat"), TEXT("punch")};
		if (GodfreyCueTokenEqualsAny(Token, EmphasisTokens, UE_ARRAY_COUNT(EmphasisTokens)))
		{
			TriggerEmphasis();
			return true;
		}
		static const TCHAR* const SeriousTokens[] = {TEXT("serious"), TEXT("stern"), TEXT("somber")};
		if (GodfreyCueTokenEqualsAny(Token, SeriousTokens, UE_ARRAY_COUNT(SeriousTokens)))
		{
			TriggerSerious();
			return true;
		}
		static const TCHAR* const AmusedTokens[] = {
			TEXT("amused"), TEXT("amus"), TEXT("humor"), TEXT("laugh"), TEXT("smile")};
		if (GodfreyCueTokenEqualsAny(Token, AmusedTokens, UE_ARRAY_COUNT(AmusedTokens)))
		{
			TriggerAmused();
			return true;
		}
		static const TCHAR* const ListenTokens[] = {TEXT("listening"), TEXT("listen")};
		if (GodfreyCueTokenEqualsAny(Token, ListenTokens, UE_ARRAY_COUNT(ListenTokens)))
		{
			BeginListening();
			return true;
		}
		static const TCHAR* const ThinkTokens[] = {TEXT("thinking"), TEXT("think")};
		if (GodfreyCueTokenEqualsAny(Token, ThinkTokens, UE_ARRAY_COUNT(ThinkTokens)))
		{
			BeginThinking();
			return true;
		}
		static const TCHAR* const SpeakTokens[] = {TEXT("speaking"), TEXT("speak"), TEXT("talk")};
		if (GodfreyCueTokenEqualsAny(Token, SpeakTokens, UE_ARRAY_COUNT(SpeakTokens)))
		{
			BeginSpeaking();
			return true;
		}
		static const TCHAR* const IdleTokens[] = {TEXT("idle"), TEXT("neutral")};
		if (GodfreyCueTokenEqualsAny(Token, IdleTokens, UE_ARRAY_COUNT(IdleTokens)))
		{
			ReturnToIdle();
			return true;
		}
		static const TCHAR* const FarewellTokens[] = {TEXT("farewell"), TEXT("goodbye")};
		if (GodfreyCueTokenEqualsAny(Token, FarewellTokens, UE_ARRAY_COUNT(FarewellTokens)))
		{
			BeginFarewell();
			return true;
		}

		// Legacy substring fallback only for short free-form cue strings.
		if (Token.Len() <= 12)
		{
			if (GodfreyCueTokenContainsKeyword(Token, TEXT("emphas")) || GodfreyCueTokenContainsKeyword(Token, TEXT("stress")))
			{
				TriggerEmphasis();
				return true;
			}
			if (GodfreyCueTokenContainsKeyword(Token, TEXT("serious")) || GodfreyCueTokenContainsKeyword(Token, TEXT("stern")))
			{
				TriggerSerious();
				return true;
			}
			if (GodfreyCueTokenContainsKeyword(Token, TEXT("amus")) || GodfreyCueTokenContainsKeyword(Token, TEXT("laugh")))
			{
				TriggerAmused();
				return true;
			}
			if (GodfreyCueTokenContainsKeyword(Token, TEXT("listen")))
			{
				BeginListening();
				return true;
			}
			if (GodfreyCueTokenContainsKeyword(Token, TEXT("think")))
			{
				BeginThinking();
				return true;
			}
			if (GodfreyCueTokenContainsKeyword(Token, TEXT("speak")) || GodfreyCueTokenContainsKeyword(Token, TEXT("talk")))
			{
				BeginSpeaking();
				return true;
			}
		}
		return false;
	};

	bool bHit = false;
	if (!V.IsEmpty())
	{
		bHit = RouteCoarseValue(V);
	}
	if (!bHit && !T.IsEmpty() && !bTypeIsState)
	{
		bHit = RouteCoarseValue(T);
	}

	if (bHit)
	{
		UE_LOG(LogGodfreyPerformance, Log, TEXT("GodfreyPerformer: routed cue (type=\"%s\" value=\"%s\")"), *T, *V);
	}
	return bHit;
}

void UGodfreyPerformanceStateComponent::ApplyPerformanceState(const EGodfreyPerformanceState NewState)
{
	const EGodfreyPerformanceState Previous = PerformanceState;
	PerformanceState = NewState;
	UE_LOG(LogGodfreyPerformance, Log, TEXT("GodfreyPerformer: performance state %d -> %d"), static_cast<int32>(Previous),
		static_cast<int32>(NewState));

	OnPerformanceStateChanged.Broadcast(PerformanceState, Previous);

	if (Previous == EGodfreyPerformanceState::Speaking && NewState != EGodfreyPerformanceState::Speaking)
	{
		OnSpeakingEnded.Broadcast();
	}

	DispatchEnteredStateDelegates(NewState, Previous);
}

void UGodfreyPerformanceStateComponent::DispatchEnteredStateDelegates(const EGodfreyPerformanceState NewState,
	const EGodfreyPerformanceState PreviousState)
{
	switch (NewState)
	{
	case EGodfreyPerformanceState::Idle:
		if (PreviousState != EGodfreyPerformanceState::Idle)
		{
			OnReturnedToIdle.Broadcast();
		}
		break;
	case EGodfreyPerformanceState::Listening:
		if (PreviousState != EGodfreyPerformanceState::Listening)
		{
			OnListeningStarted.Broadcast();
		}
		break;
	case EGodfreyPerformanceState::Thinking:
		if (PreviousState != EGodfreyPerformanceState::Thinking)
		{
			OnThinkingStarted.Broadcast();
		}
		break;
	case EGodfreyPerformanceState::Speaking:
		if (PreviousState != EGodfreyPerformanceState::Speaking)
		{
			OnSpeakingStarted.Broadcast();
		}
		break;
	case EGodfreyPerformanceState::Emphasising:
		if (PreviousState != EGodfreyPerformanceState::Emphasising)
		{
			OnEmphasisTriggered.Broadcast();
		}
		break;
	case EGodfreyPerformanceState::Serious:
		if (PreviousState != EGodfreyPerformanceState::Serious)
		{
			OnSeriousTriggered.Broadcast();
		}
		break;
	case EGodfreyPerformanceState::Amused:
		if (PreviousState != EGodfreyPerformanceState::Amused)
		{
			OnAmusedTriggered.Broadcast();
		}
		break;
	default:
		break;
	}
}

void UGodfreyPerformanceStateComponent::SetPendingPostEngageState(const EGodfreyPerformanceState State)
{
	if (!bEnableExhibitionPresence)
	{
		return;
	}
	PendingPostEngageState = State;
}

void UGodfreyPerformanceStateComponent::ApplyPendingPostEngageState()
{
	if (!bEnableExhibitionPresence)
	{
		PendingPostEngageState = EGodfreyPerformanceState::Idle;
		return;
	}

	const EGodfreyPerformanceState Pending = PendingPostEngageState;
	PendingPostEngageState = EGodfreyPerformanceState::Idle;
	if (Pending == EGodfreyPerformanceState::Listening)
	{
		return;
	}
	if (Pending == EGodfreyPerformanceState::Thinking)
	{
		EnterThinking();
		return;
	}
	if (Pending == EGodfreyPerformanceState::Speaking)
	{
		EnterSpeaking();
	}
}
