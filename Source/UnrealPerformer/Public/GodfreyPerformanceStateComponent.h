#pragma once

#include "UnrealPerformerApi.h"
#include "Components/ActorComponent.h"
#include "GodfreyPerformanceTypes.h"
#include "GodfreyPerformanceStateComponent.generated.h"

/**
 * Godfrey Performer v1 — Blueprint-facing behaviour event bus for exhibition characters.
 *
 * How to use from Blueprint (recommended wiring):
 * - Add this component to BP_Godfrey / BP_Gavin (or any actor passed as CharacterForAce to StreamGodfreySpeechToAudio).
 * - The Godfrey Brain async action already calls NotifyUtteranceStarted / NotifyUtteranceEnded / NotifyPerformanceCue on this component when present.
 * - Bind OnPerformanceStateChanged for AnimBP / layered state machines; bind OnListeningStarted, OnSpeakingStarted, etc. for one-shot montages or additive slots.
 * - For STT / local UX, call BeginListening / BeginThinking from your UI or voice subsystem.
 * - When bAutoSpeakingStateFromUtterance is true (default), NotifyUtteranceStarted/Ended from the speech stream drive BeginSpeaking/EndSpeaking so the animation bridge receives OnSpeakingStarted without Blueprint wiring.
 * - Performance cues: with bRoutePerformanceCuesToStates enabled (default), known cue "type" strings from the Brain JSON are mapped to Begin / Trigger helpers; unknown types still raise OnPerformanceCueReceived so you can branch in BP.
 *
 * This component does not drive ACE, PCM streaming, or Control Rig — keep procedural face/body in MetaHuman layers; react here with montages, look targets, and attention logic.
 */
UCLASS(ClassGroup = (Godfrey), meta = (BlueprintSpawnableComponent))
class UNREAL_PERFORMER_API UGodfreyPerformanceStateComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UGodfreyPerformanceStateComponent();

	// --- Explicit v1 orchestration API (preferred names for Blueprint graphs) ---

	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performer")
	void BeginListening();

	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performer")
	void BeginThinking();

	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performer")
	void BeginSpeaking();

	/** Leaves Speaking (no-op if not Speaking). Fires OnSpeakingEnded; default transition is Idle — bind and call BeginListening if you want mic-open posture instead. */
	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performer")
	void EndSpeaking();

	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performer")
	void ReturnToIdle();

	/** Enters Emphasising; if already Emphasising, still broadcasts OnEmphasisTriggered so montages can retrigger. */
	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performer")
	void TriggerEmphasis();

	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performer")
	void TriggerAmused();

	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performer")
	void TriggerSerious();

	// --- Original state API (kept for compatibility; forwards into the same state machine) ---

	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performance")
	bool TrySetPerformanceState(EGodfreyPerformanceState NewState);

	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performance")
	void EnterIdle();

	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performance")
	void EnterListening();

	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performance")
	void EnterThinking();

	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performance")
	void EnterSpeaking();

	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performance")
	void EnterEmphasising();

	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performance")
	void EnterSerious();

	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performance")
	void EnterAmused();

	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performance")
	void ResetToIdle();

	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performance")
	void NotifyUtteranceStarted();

	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performance")
	void NotifyUtteranceEnded();

	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performance")
	void NotifyPerformanceCue(const FString& CueType, const FString& CueValue, const FString& RawCue);

	/** Convenience for middleware named library actions (forwards as type=action). */
	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performance")
	void NotifyNamedPerformanceAction(const FString& ActionId);

	/**
	 * Exhibition poll saw Brain phase=awaiting_reply (question accepted, LLM still running).
	 * Starts Listening with awaiting-brain semantics so the animation bridge can play a real listening montage.
	 * Deduped per RequestId across ~1s polls.
	 */
	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performer")
	void NotifyReplyIncoming(const FString& RequestId);

	/** Clears awaiting-brain flag (LLM error / pending expired / speaking started). May refresh visitor-await listening. */
	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performer")
	void ClearReplyIncoming(bool bRefreshVisitorAwaitListening = true);

	UFUNCTION(BlueprintPure, Category = "Godfrey|Performer")
	bool IsAwaitingBrainReply() const { return bAwaitingBrainReply; }

	UFUNCTION(BlueprintPure, Category = "Godfrey|Performer")
	FString GetAwaitingBrainRequestId() const { return AwaitingBrainRequestId; }

	/** First visitor turn while looking at sea — turn/greet then Listening. Safe to call repeatedly. */
	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performer|Presence")
	void NotifyVisitorEngaged();

	/** Farewell wave + return to sea idle (goodbye cue or idle timeout). */
	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performer|Presence")
	void BeginFarewell();

	/**
	 * Brain reported the visitor ended the visit (conversationEnd flag / [farewell] cue).
	 * Latches the request; Unreal starts the farewell once the current reply has finished speaking (R12),
	 * so Godfrey never waves goodbye mid-sentence.
	 */
	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performer|Presence")
	void RequestConversationEnd(const FString& Reason);

	/** Drops a latched conversation end (visitor asked something else before the farewell ran). */
	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performer|Presence")
	void CancelPendingConversationEnd(const FString& Reason);

	UFUNCTION(BlueprintPure, Category = "Godfrey|Performer|Presence")
	bool IsConversationEndPending() const { return bConversationEndRequested; }

	/** Called by animation bridge when greet chain finishes — enter Conversing + Listening. */
	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performer|Presence")
	void NotifyEngageSequenceFinished();

	/** Called by animation bridge when farewell chain finishes — return to sea idle. */
	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performer|Presence")
	void NotifyFarewellSequenceFinished();

	/** Mark visitor activity so the idle timeout resets (listen/think/speak/cues). */
	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performer|Presence")
	void NotifyVisitorActivity();

	/**
	 * Visitor STT speech_started — ends the R10 silent-period timer immediately
	 * (do not wait for transcript_completed).
	 */
	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performer|Presence")
	void NotifyVisitorSpeechBegan();

	/**
	 * Visitor STT speech_stopped — start of silence after they pause; restarts the R10 clock
	 * so engagement waits a full DialogEngageSilenceSeconds for a final (or another utterance).
	 */
	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performer|Presence")
	void NotifyVisitorSpeechEnded();

	/**
	 * Visitor produced a spoken turn (STT accepted). Ends the R10 silent period
	 * (timer restarts only on the next Speak-green / post-speech silence).
	 */
	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performer|Presence")
	void NotifyVisitorSpoke();

	/**
	 * Mic/lantern just opened for the visitor (Speak green). Starts the R10 silent-period clock.
	 */
	UFUNCTION(BlueprintCallable, Category = "Godfrey|Performer|Presence")
	void NotifyListeningWindowOpened();

	UFUNCTION(BlueprintPure, Category = "Godfrey|Performance")
	EGodfreyPerformanceState GetPerformanceState() const { return PerformanceState; }

	UFUNCTION(BlueprintPure, Category = "Godfrey|Performer|Presence")
	EGodfreyExhibitionPresence GetExhibitionPresence() const { return ExhibitionPresence; }

	UFUNCTION(BlueprintPure, Category = "Godfrey|Performer|Presence")
	bool HasEngagedVisitor() const { return bHasEngagedVisitor; }

	/**
	 * In-dialog: visitor has spoken (engage started) and farewell has not begun.
	 * Out-of-dialog: game start / SeaIdle, or after farewell AS has started (returning to sea).
	 * Animation idle pools and sea-looking AS are gated on this.
	 */
	UFUNCTION(BlueprintPure, Category = "Godfrey|Performer|Presence")
	bool IsInDialog() const;

	/** When true, NotifyPerformanceCue maps well-known Brain cue types to BeginX / TriggerX helpers (see cpp). Unknown types are only logged and forwarded via OnPerformanceCueReceived. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer")
	bool bRoutePerformanceCuesToStates = true;

	/**
	 * Prefer type=state for coarse listening/thinking/speaking/… transitions.
	 * type=action|performance|gesture values are left for the animation bridge named-action library.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer")
	bool bPreferExplicitStateCueType = true;

	/**
	 * When true, ACE/stream utterance hooks also drive the performance Speaking state (BeginSpeaking on start, EndSpeaking on end).
	 * Keeps UGodfreyPerformerAnimationBridgeComponent in sync during real playback without Blueprint wiring on BP_Gavin.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer")
	bool bAutoSpeakingStateFromUtterance = true;

	/** Exhibition presence director: sea idle at BeginPlay, greet on first engage, farewell after quiet timeout. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Presence")
	bool bEnableExhibitionPresence = true;

	/** After last visitor activity while engaged, begin farewell (seconds). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Presence", meta = (ClampMin = "10", ClampMax = "600"))
	float VisitorIdleTimeoutSeconds = 60.f;

	/**
	 * While in dialog with a present visitor, after this many seconds of Speak-green silence
	 * (no visitor and no Godfrey speech), UE asks Godfrey to continue (R10).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Presence", meta = (ClampMin = "1.0", ClampMax = "30.0"))
	float DialogEngageSilenceSeconds = 8.0f;

	/** UE-owned conversational nudge while Present + in dialog (R10). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Presence")
	bool bEnableDialogEngagementPrompts = true;

	/**
	 * Prompt for the silence nudge. Brain continues from session history; ask for name when unknown.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Presence", meta = (MultiLine = "true", EditCondition = "bEnableDialogEngagementPrompts"))
	FString DialogEngagePrompt = TEXT(
		"(The visitor has been quiet for a few seconds. Continue the conversation naturally in one or two short sentences that follow from what was just said. If you do not yet know their name, ask for it. Otherwise ask a brief follow-up or offer a short remark. Stay in character. Do not say goodbye.)");

	/**
	 * Safety net for a latched conversation end: if no speech is playing for this long after the request
	 * (reply never streamed, utterance-ended hook missed), run the farewell anyway. The timer restarts while
	 * Godfrey is speaking, so a long goodbye reply is never cut short.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Presence", meta = (ClampMin = "2", ClampMax = "120"))
	float ConversationEndFallbackSeconds = 15.f;

	/** When true, EndSpeaking / ReturnToIdle while engaged goes to Listening instead of frozen Idle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Godfrey|Performer|Presence")
	bool bReturnToListeningWhileEngaged = true;

	// --- Delegates (behaviour event bus) ---

	UPROPERTY(BlueprintAssignable, Category = "Godfrey|Performer")
	FGodfreyPerformanceStateChangedEvent OnPerformanceStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "Godfrey|Performer")
	FGodfreyPerformerSimpleEvent OnListeningStarted;

	UPROPERTY(BlueprintAssignable, Category = "Godfrey|Performer")
	FGodfreyPerformerSimpleEvent OnThinkingStarted;

	UPROPERTY(BlueprintAssignable, Category = "Godfrey|Performer")
	FGodfreyPerformerSimpleEvent OnSpeakingStarted;

	UPROPERTY(BlueprintAssignable, Category = "Godfrey|Performer")
	FGodfreyPerformerSimpleEvent OnSpeakingEnded;

	UPROPERTY(BlueprintAssignable, Category = "Godfrey|Performer")
	FGodfreyPerformerSimpleEvent OnReturnedToIdle;

	UPROPERTY(BlueprintAssignable, Category = "Godfrey|Performer")
	FGodfreyPerformerSimpleEvent OnEmphasisTriggered;

	UPROPERTY(BlueprintAssignable, Category = "Godfrey|Performer")
	FGodfreyPerformerSimpleEvent OnAmusedTriggered;

	UPROPERTY(BlueprintAssignable, Category = "Godfrey|Performer")
	FGodfreyPerformerSimpleEvent OnSeriousTriggered;

	/** Raw cue from Godfrey Brain (type/value/raw JSON fragment). Always fired from NotifyPerformanceCue after routing pass. */
	UPROPERTY(BlueprintAssignable, Category = "Godfrey|Performer")
	FGodfreyPerformerCueEvent OnPerformanceCueReceived;

	UPROPERTY(BlueprintAssignable, Category = "Godfrey|Performer|Presence")
	FGodfreyExhibitionPresenceChangedEvent OnExhibitionPresenceChanged;

	UPROPERTY(BlueprintAssignable, Category = "Godfrey|Performer|Presence")
	FGodfreyPerformerSimpleEvent OnSeaIdleStarted;

	UPROPERTY(BlueprintAssignable, Category = "Godfrey|Performer|Presence")
	FGodfreyPerformerSimpleEvent OnEngageSequenceStarted;

	UPROPERTY(BlueprintAssignable, Category = "Godfrey|Performer|Presence")
	FGodfreyPerformerSimpleEvent OnFarewellSequenceStarted;

	/** PCM / lipsync utterance hooks (from async stream); distinct from performance Speaking state — wire both if you want mouth open on audio and body state separately. */
	UPROPERTY(BlueprintAssignable, Category = "Godfrey|Performance")
	FGodfreyUtteranceLifecycleEvent OnGodfreyUtteranceStarted;

	UPROPERTY(BlueprintAssignable, Category = "Godfrey|Performance")
	FGodfreyUtteranceLifecycleEvent OnGodfreyUtteranceEnded;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	void ApplyPerformanceState(EGodfreyPerformanceState NewState);
	void DispatchEnteredStateDelegates(EGodfreyPerformanceState NewState, EGodfreyPerformanceState PreviousState);
	static FString NormalizeCueToken(const FString& In);
	bool TryConsumePerformanceCueForRouting(const FString& CueType, const FString& CueValue);
	void SetPendingPostEngageState(EGodfreyPerformanceState State);
	void ApplyPendingPostEngageState();

	void SetExhibitionPresence(EGodfreyExhibitionPresence NewPresence);
	void EnterSeaIdlePresence();
	void TickVisitorIdleTimeout(float DeltaTime);
	void TickDialogEngagement(float DeltaTime);
	void MarkDialogExchange();
	bool TryFireDialogEngagementPrompt();
	bool IsVisitorVisiblyPresent() const;
	bool IsVisitorListenWindowOpen() const;
	bool IsGodfreyBusyForDialogEngage() const;
	/** Runs a latched conversation end if speech has stopped long enough (utterance-ended hook missed). */
	void TickPendingConversationEnd(float DeltaTime);
	/** Executes the latched farewell and clears the latch. */
	void RunPendingConversationEnd(const TCHAR* Trigger);
	bool LooksLikeFarewellCue(const FString& CueType, const FString& CueValue) const;

	UPROPERTY(Transient)
	EGodfreyPerformanceState PerformanceState = EGodfreyPerformanceState::Idle;

	UPROPERTY(Transient)
	EGodfreyExhibitionPresence ExhibitionPresence = EGodfreyExhibitionPresence::SeaIdle;

	UPROPERTY(Transient)
	bool bHasEngagedVisitor = false;

	UPROPERTY(Transient)
	EGodfreyPerformanceState PendingPostEngageState = EGodfreyPerformanceState::Idle;

	double LastVisitorActivityWorldTime = -1.0;

	/** Last visitor STT turn or Godfrey audible end — drives R10 conversational nudge. */
	double LastDialogExchangeWorldTime = -1.0;

	/** True while Brain has accepted a question and has not yet queued TTS (exhibition awaiting_reply). */
	UPROPERTY(Transient)
	bool bAwaitingBrainReply = false;

	UPROPERTY(Transient)
	FString AwaitingBrainRequestId;

	/** Brain said the visit is over; farewell waits for the current reply to finish (R12). */
	UPROPERTY(Transient)
	bool bConversationEndRequested = false;

	UPROPERTY(Transient)
	FString ConversationEndReason;

	/** World time the fallback measures from; pushed forward while Godfrey is still speaking. */
	double ConversationEndWaitStartWorldTime = -1.0;
};
