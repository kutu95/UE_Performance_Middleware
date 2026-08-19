#pragma once



#include "UnrealPerformerApi.h"
#include "CoreMinimal.h"

#include "Engine/DeveloperSettings.h"
#include "InputCoreTypes.h"

#include "UnrealPerformerGodfreySettings.generated.h"



/**

 * Project settings for Godfrey PCM → ACE streaming (chunk size, burst override, diagnostics).

 * Edit under Project Settings → Plugins → Unreal Performer (Godfrey / ACE), or DefaultEngine.ini
 * [/Script/UnrealPerformer.UnrealPerformerGodfreySettings] (Config=Engine).

 */

UCLASS(Config = Engine, DefaultConfig, meta = (DisplayName = "Unreal Performer (Godfrey / ACE)"))

class UNREAL_PERFORMER_API UUnrealPerformerGodfreySettings : public UDeveloperSettings

{

	GENERATED_BODY()



public:

	UUnrealPerformerGodfreySettings();



	/**

	 * Upper bound on PCM duration per AnimateFromAudioSamples sub-chunk inside PushPcm16Chunk (clamped 10–80 ms in code).

	 * The HTTP drain path uses the same value so chunk sizes stay aligned end-to-end.

	 */

	UPROPERTY(Config, EditAnywhere, Category = "ACE Ingest", meta = (ClampMin = "10.0", ClampMax = "80.0", UIMin = "10.0", UIMax = "80.0"))

	float AceMaxPcmPushChunkDurationMs = 55.f;



	/** If true, UACEBlueprintLibrary::OverrideA2F3DInferenceMode(true) runs when the game module starts (burst mode). */

	UPROPERTY(Config, EditAnywhere, Category = "ACE Ingest")

	bool bApplyAceBurstInferenceOverrideAtStartup = true;



	/** Max AnimateFromAudioSamples pushes per HTTP drain tick on the game thread. */

	UPROPERTY(Config, EditAnywhere, Category = "ACE Ingest", meta = (ClampMin = "1", ClampMax = "16", UIMin = "1", UIMax = "8"))

	int32 GodfreyAcePrePlayPushBudgetPerTick = 6;



	/**
	 * After Play(): soft push-budget throttle when sent audio exceeds curves.
	 * Keep ON for early-play (hold-play off) — otherwise HTTP floods A2F and EndAudioSamples hitchs the game thread for seconds mid-speech.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "ACE Ingest")
	bool bGodfreyAcePaceIngestByCurveCatchUp = true;

	/** During playback: at or above this (sent − maxCurveTs), limit to 1 push per delayed drain tick. */
	UPROPERTY(Config, EditAnywhere, Category = "ACE Ingest", meta = (ClampMin = "0.35", ClampMax = "1.5", UIMin = "0.4", UIMax = "0.8", EditCondition = "bGodfreyAcePaceIngestByCurveCatchUp"))
	float GodfreyAceMaxUnmatchedAudioSeconds = 0.65f;

	/** During playback: at or above this (sent − maxCurveTs), limit to half the configured push budget. */
	UPROPERTY(Config, EditAnywhere, Category = "ACE Ingest", meta = (ClampMin = "0.2", ClampMax = "1.0", UIMin = "0.25", UIMax = "0.5", EditCondition = "bGodfreyAcePaceIngestByCurveCatchUp"))
	float GodfreyAceSoftThrottleMediumUnmatchedSeconds = 0.45f;

	/**
	 * After HTTP+PCM drain, defer EndAudioSamples while (sent − maxCurveTs) exceeds
	 * GodfreyAceEndAudioMaxUnmatchedSeconds and ACE is already playing.
	 *
	 * Off by default: streaming A2F withholds the last ~0.5-0.9s of curves until end-of-stream is
	 * signalled, so waiting for curves to catch up before calling EndAudioSamples cannot succeed.
	 * The wait ends only via the watchdog or the catch-up timeout, by which point the tail of the
	 * utterance has already played with no curves - which is the audible dropout and frozen lip
	 * sync at the end of every speak. Enable only to restore the old hitch-avoidance behaviour.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "ACE Ingest")
	bool bGodfreyDeferEndAudioSamplesForCurveCatchUp = false;

	/** Unmatched-seconds threshold used when bGodfreyDeferEndAudioSamplesForCurveCatchUp is enabled. */
	UPROPERTY(Config, EditAnywhere, Category = "ACE Ingest", meta = (ClampMin = "0.15", ClampMax = "2.0", UIMin = "0.25", UIMax = "0.8", EditCondition = "bGodfreyDeferEndAudioSamplesForCurveCatchUp"))
	float GodfreyAceEndAudioMaxUnmatchedSeconds = 0.45f;

	/** Cap how long FinishStream waits for curve catch-up before forcing EndAudioSamples anyway. */
	UPROPERTY(Config, EditAnywhere, Category = "ACE Ingest", meta = (ClampMin = "1.0", ClampMax = "60.0", UIMin = "5.0", UIMax = "30.0"))
	float GodfreyAceEndAudioCatchUpTimeoutSeconds = 20.f;



	/** One line at FinishStream with utterance-relative timings. */

	UPROPERTY(Config, EditAnywhere, Category = "Diagnostics")

	bool bLogUtteranceLatencySummaryAtStreamFinish = true;



	/** At FinishStream: Godfrey-side startup summary (PCM vs OnAnimationStarted). */

	UPROPERTY(Config, EditAnywhere, Category = "Diagnostics")

	bool bLogGodfreyAceStartupCompletionSummary = true;



	/** Per AnimateFromAudioSamples sub-chunk: frames, nominal chunk ms, wall ms. */

	UPROPERTY(Config, EditAnywhere, Category = "Diagnostics")

	bool bLogPerAnimateChunkWallTime = false;

	/**
	 * Log parallel PCM + ACE audible state (mixer device, AudioComponent play state, procedural queue depth,
	 * underflows). Emits snapshots at parallel start, periodic ticks during playback, and FinishStream.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Diagnostics")
	bool bGodfreyLogAudiblePlaybackDiagnostics = true;



	/** Default silence duration for WarmupAcePipeline / UGodfreyAceWarmupComponent. */

	UPROPERTY(Config, EditAnywhere, Category = "Warmup", meta = (ClampMin = "0.1", ClampMax = "5.0"))

	float WarmupSilenceSeconds = 0.55f;



	/** If true, AllocateA2F3DResources at game startup (LocalA2F-Mark TRT prep). */

	UPROPERTY(Config, EditAnywhere, Category = "Warmup")

	bool bAllocateAceProviderResourcesAtGameStartup = true;



	UPROPERTY(Config, EditAnywhere, Category = "Warmup")

	FName GodfreyAceProviderNameForStartupAllocation = FName(TEXT("LocalA2F-Mark"));



	UPROPERTY(Config, EditAnywhere, Category = "Warmup", meta = (ClampMin = "0.0", ClampMax = "10.0", UIMin = "0.0", UIMax = "2.0"))

	float GodfreyAceWarmupBeginPlayDelaySeconds = 0.2f;



	UPROPERTY(Config, EditAnywhere, Category = "Warmup")

	bool bAllocateAceProviderResourcesBeforeCharacterWarmup = true;



	UPROPERTY(Config, EditAnywhere, Category = "Warmup")

	bool bMuteAceAudioOutputDuringWarmup = true;



	/** Temporarily raise ACE BufferLengthInSeconds during each utterance (restored on finish/stop). */

	UPROPERTY(Config, EditAnywhere, Category = "ACE Playback Priming")

	bool bApplyGodfreyAceBufferLength = true;



	UPROPERTY(Config, EditAnywhere, Category = "ACE Playback Priming", meta = (ClampMin = "0.05", ClampMax = "1.5", UIMin = "0.05", UIMax = "1.5"))

	float GodfreyAceBufferLengthSeconds = 0.35f;



	/** If >= 0, overrides MinBlendShapeSamplesBeforePlay for the utterance. Use -1 for ACE component default. */

	UPROPERTY(Config, EditAnywhere, Category = "ACE Playback Priming", meta = (ClampMin = "-1", ClampMax = "64", UIMin = "-1", UIMax = "32"))

	int32 GodfreyAceMinBlendShapeSamplesOverride = 16;



	/** If >= 0, overrides MinCurveTimestampSecondsBeforePlay. Use -1 for ACE component default (recommended). */

	UPROPERTY(Config, EditAnywhere, Category = "ACE Playback Priming", meta = (ClampMin = "-1", ClampMax = "3", UIMin = "-1", UIMax = "2"))

	float GodfreyAceMinCurveTimestampBeforePlay = 1.05f;

	/**
	 * Burst A2F: block ACE Play() until FinishStream/EndAudioSamples so the full curve batch exists before
	 * audible playback. Off by default — adds startup delay equal to full download + A2F burst on long clips.
	 * Use when lip sync quality matters more than time-to-first-word on long monolithic replies.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "ACE Playback Priming")
	bool bGodfreyAceHoldPlayUntilStreamEnd = false;

	/** While bGodfreyAceHoldPlayUntilStreamEnd is active during ingest, MinCurveTimestampSecondsBeforePlay uses this impossibly high value. */
	UPROPERTY(Config, EditAnywhere, Category = "ACE Playback Priming", meta = (ClampMin = "100.0", ClampMax = "100000.0", EditCondition = "bGodfreyAceHoldPlayUntilStreamEnd"))
	float GodfreyAceHoldPlayMinCurveTimestampGate = 99999.f;



	/**
	 * After FinishStream, keep OnAnimationStarted bound for up to this many seconds so late ACE callbacks still fire.
	 * If OnAnimationStarted never arrives within this grace (ACE-only audible), the utterance errors out.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "ACE Lifecycle", meta = (ClampMin = "0.0", ClampMax = "60.0", UIMin = "0.0", UIMax = "30.0"))
	float GodfreyAcePostFinishOnAnimationStartedDelegateGraceSeconds = 10.f;

	/**
	 * DEPRECATED dual-clock path. Must stay false for exhibition: ACE alone drives heard audio + lipsync.
	 * Enabling reintroduces parallel WavUrl playback that can drift from ACE lipsync.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "ACE Playback Priming|Deprecated Dual Clock")
	bool bGodfreyUseParallelPcmAudiblePlayback = false;

	/** Only used if parallel path is forced on (not supported for exhibition). */
	UPROPERTY(Config, EditAnywhere, Category = "ACE Playback Priming|Deprecated Dual Clock", meta = (EditCondition = "bGodfreyUseParallelPcmAudiblePlayback"))
	bool bGodfreyMuteAceWhenParallelAudibleStarts = false;

	/** Unused when parallel is off (ACE-only). */
	UPROPERTY(Config, EditAnywhere, Category = "ACE Playback Priming|Deprecated Dual Clock")
	bool bGodfreyUpsamplePcmToMixerRate = true;

	/** Unused when parallel is off — early FinishStream play was the main lipsync/audio drift source. */
	UPROPERTY(Config, EditAnywhere, Category = "ACE Playback Priming|Deprecated Dual Clock", meta = (EditCondition = "bGodfreyUseParallelPcmAudiblePlayback"))
	bool bGodfreyPlayAudibleAtFinishStream = false;

	/** Unused when parallel is off. */
	UPROPERTY(Config, EditAnywhere, Category = "ACE Playback Priming|Deprecated Dual Clock", meta = (EditCondition = "bGodfreyUseParallelPcmAudiblePlayback"))
	bool bGodfreyAudibleSpawnAtPlayerLocation = true;

	// --- Phase 1 hardening (diagnostics / config; defaults preserve current behaviour) ---

	/** Emit structured [Speech]/[ACE]/[Audio]/… pipeline stage lines correlated by SpeechId. */
	UPROPERTY(Config, EditAnywhere, Category = "Diagnostics|Pipeline")
	bool bGodfreyStructuredPipelineLogging = true;

	/** Emit [Performance] TimingMs summary when an utterance completes. */
	UPROPERTY(Config, EditAnywhere, Category = "Diagnostics|Pipeline")
	bool bGodfreyLogUtteranceTimingMs = true;

	/** Show the Godfrey runtime performance overlay in PIE / Development builds. */
	UPROPERTY(Config, EditAnywhere, Category = "Diagnostics|HUD")
	bool bGodfreyShowRuntimePerfHud = true;

	/** Toggle key for the runtime performance overlay (PIE). */
	UPROPERTY(Config, EditAnywhere, Category = "Diagnostics|HUD")
	FKey GodfreyPerfHudToggleKey = EKeys::F8;

	/** Auto-spawn webcam visitor presence on exhibition GameMode BeginPlay. */
	UPROPERTY(Config, EditAnywhere, Category = "Vision|Presence")
	bool bGodfreyEnableVisitorPresenceWebcam = true;

	/** Show bottom-left webcam debug preview in PIE / Development (off by default; F9 still toggles). */
	UPROPERTY(Config, EditAnywhere, Category = "Vision|Presence")
	bool bGodfreyShowWebcamDebugPreview = false;

	/** Toggle key for the webcam debug preview. */
	UPROPERTY(Config, EditAnywhere, Category = "Vision|Presence")
	FKey GodfreyWebcamPreviewToggleKey = EKeys::F9;

	/** Toggle key to force occupied (debug presence without standing in frame). */
	UPROPERTY(Config, EditAnywhere, Category = "Vision|Presence")
	FKey GodfreyWebcamForceOccupiedToggleKey = EKeys::F10;

	/** Recapture the empty-room webcam background (PIE / Development). */
	UPROPERTY(Config, EditAnywhere, Category = "Vision|Presence")
	FKey GodfreyWebcamRecaptureEmptyKey = EKeys::F11;

	/** Substring filter for webcam device name; empty = first enumerated webcam. */
	UPROPERTY(Config, EditAnywhere, Category = "Vision|Presence")
	FString GodfreyWebcamDeviceNameFilter;

	/**
	 * Ignore webcam occupancy for this many seconds after the camera starts (time to step out of frame),
	 * then capture the empty background.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Vision|Presence", meta = (ClampMin = "0.0", ClampMax = "30.0"))
	float GodfreyWebcamStartupIgnoreSeconds = 2.f;

	/** Seconds of continuous occupancy before Present / Welcome engage. */
	UPROPERTY(Config, EditAnywhere, Category = "Vision|Presence", meta = (ClampMin = "0.2", ClampMax = "10.0"))
	float GodfreyWebcamEnterDwellSeconds = 1.75f;

	/** Seconds of continuous emptiness before Empty / farewell. 0 = immediate on leave. */
	UPROPERTY(Config, EditAnywhere, Category = "Vision|Presence", meta = (ClampMin = "0.0", ClampMax = "20.0"))
	float GodfreyWebcamLeaveDwellSeconds = 2.f;

	/**
	 * Occupancy must fall below this to leave Present (hysteresis vs enter threshold 0.06).
	 * Stops a still visitor whose blob dips just under 6% from triggering goodbye.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Vision|Presence", meta = (ClampMin = "0.01", ClampMax = "0.5"))
	float GodfreyWebcamOccupancyLeaveFractionThreshold = 0.03f;

	/** Loop a video on MediaPlate2 in PIE/game (file under Content/, e.g. Movies/foo.mp4). Falls back to Stage_Backdrop if no plate. */
	UPROPERTY(Config, EditAnywhere, Category = "Exhibit|Backdrop")
	bool bGodfreyEnableStageBackdropVideo = true;

	UPROPERTY(Config, EditAnywhere, Category = "Exhibit|Backdrop")
	FString GodfreyStageBackdropVideoContentPath = TEXT("Movies/Fremantle Harbour 1890_H264.mp4");

	/** Hide Exhibit_Floor in PIE/game so Godfrey reads as standing in the backdrop video. Collision stays. */
	UPROPERTY(Config, EditAnywhere, Category = "Exhibit|Backdrop")
	bool bGodfreyHideExhibitFloorInPlay = true;

	/** When Present while SeaIdle, arm Welcome and call NotifyVisitorEngaged (R17). */
	UPROPERTY(Config, EditAnywhere, Category = "Vision|Presence")
	bool bGodfreyPresenceEngageOnDwell = true;

	/** When presence engages, also AskGodfrey for a short spoken welcome. */
	UPROPERTY(Config, EditAnywhere, Category = "Vision|Presence")
	bool bGodfreyPresenceWelcomeSpeak = true;

	/** Brain prompt for the presence-welcome spoken turn. */
	UPROPERTY(Config, EditAnywhere, Category = "Vision|Presence", meta = (MultiLine = "true", EditCondition = "bGodfreyPresenceWelcomeSpeak"))
	FString GodfreyPresenceWelcomeSpeakPrompt = TEXT(
		"(A visitor has just approached and stands before you. Welcome them warmly in one or two short sentences, in character. Do not wait for them to speak first.)");

	/** When webcam leave-dwell completes while in dialog, spoken goodbye + farewell gesture. */
	UPROPERTY(Config, EditAnywhere, Category = "Vision|Presence")
	bool bGodfreyPresenceFarewellOnAbsence = true;

	/** Brain prompt for presence-leave goodbye (name comes from Brain visitor profile when known). */
	UPROPERTY(Config, EditAnywhere, Category = "Vision|Presence", meta = (MultiLine = "true", EditCondition = "bGodfreyPresenceFarewellOnAbsence"))
	FString GodfreyPresenceFarewellSpeakPrompt = TEXT(
		"(The visitor has walked away and left the scene. Bid them a brief goodbye — use their name if you know it. One short sentence only. End with [farewell].)");

	/** Top-right brass signal lantern Speak/Wait cue (synced to mic accept window). */
	UPROPERTY(Config, EditAnywhere, Category = "Vision|Listen Cue")
	bool bGodfreyShowListenCueLantern = true;

	/** Show Speak/Wait labels under the lantern (Constantia). */
	UPROPERTY(Config, EditAnywhere, Category = "Vision|Listen Cue", meta = (EditCondition = "bGodfreyShowListenCueLantern"))
	bool bGodfreyListenCueShowLabels = true;

	/** Keep the mic paused after ACE audible end so speaker tail is not transcribed as the visitor. */
	UPROPERTY(Config, EditAnywhere, Category = "Vision|Listen Cue", meta = (ClampMin = "0.0", ClampMax = "8.0"))
	float GodfreyPostSpeechIgnoreSeconds = 1.25f;

	/**
	 * Capture device name substring (e.g. HyperX). Empty = Windows default mic
	 * (often the webcam — wrong for exhibition STT).
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Vision|Listen Cue")
	FString GodfreyPreferredCaptureDeviceNameFilter = TEXT("HyperX");

	/**
	 * Playback device name substring. Empty = Windows default output (the Epson projector
	 * in the display room). Do not set this to HyperX — that is the mic’s headphone jack,
	 * so Godfrey would be silent on the projector while Windows sounds still play there.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Vision|Listen Cue")
	FString GodfreyPreferredPlaybackDeviceNameFilter;

	/**
	 * After STT speech_stopped, wait this long for a usable transcript. If none,
	 * AskGodfrey(MissedTranscriptPrompt) so Godfrey asks the visitor to repeat.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Vision|Listen Cue", meta = (ClampMin = "0.5", ClampMax = "8.0", EditCondition = "bGodfreyEnableMissedTranscriptPrompt"))
	float GodfreyMissedTranscriptTimeoutSeconds = 2.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Vision|Listen Cue")
	bool bGodfreyEnableMissedTranscriptPrompt = true;

	UPROPERTY(Config, EditAnywhere, Category = "Vision|Listen Cue", meta = (MultiLine = "true", EditCondition = "bGodfreyEnableMissedTranscriptPrompt"))
	FString GodfreyMissedTranscriptPrompt = TEXT(
		"(The visitor just spoke, but their words were not transcribed. In one short sentence, apologise that you did not catch that and ask them to please repeat. Stay in character as Captain Godfrey. Do not invent what they said. Do not say goodbye.)");

	/** While Present + in dialog, UE prompts Godfrey after this many Speak-green silent seconds (R10). */
	UPROPERTY(Config, EditAnywhere, Category = "Vision|Presence", meta = (ClampMin = "1.0", ClampMax = "30.0"))
	float GodfreyDialogEngageSilenceSeconds = 8.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Vision|Presence")
	bool bGodfreyEnableDialogEngagementPrompts = true;

	UPROPERTY(Config, EditAnywhere, Category = "Vision|Presence", meta = (MultiLine = "true", EditCondition = "bGodfreyEnableDialogEngagementPrompts"))
	FString GodfreyDialogEngagePrompt = TEXT(
		"(The visitor has been quiet for a few seconds. Continue the conversation naturally in one or two short sentences that follow from what was just said. If you do not yet know their name, ask for it. Otherwise ask a brief follow-up or offer a short remark. Stay in character. Do not say goodbye.)");

	/** Default exhibition queue poll interval (seconds). Components may still override locally. */
	UPROPERTY(Config, EditAnywhere, Category = "Queue", meta = (ClampMin = "0.2", ClampMax = "5.0"))
	float GodfreyDefaultQueuePollIntervalSeconds = 1.0f;

	/** Default PCM sample rate for exhibition / direct speech paths. */
	UPROPERTY(Config, EditAnywhere, Category = "Speech", meta = (ClampMin = "8000", ClampMax = "48000"))
	int32 GodfreyDefaultStreamSampleRate = 24000;

	/** Mixer sample rate used when upsampling parallel audible PCM. */
	UPROPERTY(Config, EditAnywhere, Category = "Audio", meta = (ClampMin = "16000", ClampMax = "96000"))
	int32 GodfreyMixerUpsampleSampleRate = 48000;

	/**
	 * Linear gain applied to Brain TTS PCM before it is pushed to ACE (heard audio and A2F input).
	 * Brain TTS arrives around -28 dBFS RMS / -10 dBFS peak, too quiet for exhibition playback, and ACE
	 * component Volume is clamped to 1.0 so it cannot make up the difference. Peaks above the limiter knee
	 * are soft-saturated rather than hard-clipped. 1.0 = pass through unchanged.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Audio", meta = (ClampMin = "1.0", ClampMax = "6.0", UIMin = "1.0", UIMax = "4.0"))
	float GodfreySpeechPcmGain = 2.5f;

	/**
	 * Audio2Face inputStrength. 0 = auto (1 / GodfreySpeechPcmGain) so visemes stay at native TTS
	 * amplitude while exhibition loudness stays on the gained PCM. Set >0 to override.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Audio|Lip Sync", meta = (ClampMin = "0.0", ClampMax = "2.0", UIMin = "0.0", UIMax = "1.5"))
	float GodfreyAceInputStrength = 0.f;

	/**
	 * Audio2Face lipOpenOffset. Slightly negative keeps the rest mouth closed so pause frames
	 * do not read as talking. 0 = A2F model default.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Audio|Lip Sync", meta = (ClampMin = "-0.2", ClampMax = "0.2"))
	float GodfreyAceLipOpenOffset = -0.03f;

	/**
	 * After gain, abs(sample) below this is zeroed so A2F treats TTS pauses as silence.
	 * 0 disables. 750 is about -33 dBFS after x2.5 gain.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Audio|Lip Sync", meta = (ClampMin = "0", ClampMax = "4000"))
	int32 GodfreySpeechSilenceGate = 750;

	/** Blend-out seconds when stopping speaking idle montage (animation bridge). */
	UPROPERTY(Config, EditAnywhere, Category = "Animation", meta = (ClampMin = "0.0", ClampMax = "3.0"))
	float GodfreySpeakingIdleMontageBlendOutSeconds = 1.4f;

	/** Blend-in when starting a body performance montage (smoother AS→AS transitions). */
	UPROPERTY(Config, EditAnywhere, Category = "Animation", meta = (ClampMin = "0.05", ClampMax = "1.5"))
	float GodfreyBodyMontageBlendInSeconds = 0.45f;

	/** Blend-out when stopping/replacing a body performance montage. */
	UPROPERTY(Config, EditAnywhere, Category = "Animation", meta = (ClampMin = "0.05", ClampMax = "1.5"))
	float GodfreyBodyMontageBlendOutSeconds = 0.5f;

	/** Longer blend-in for dialog listening-pool AS→AS and speak→listen (R9/R16). */
	UPROPERTY(Config, EditAnywhere, Category = "Animation", meta = (ClampMin = "0.15", ClampMax = "3.0"))
	float GodfreyDialogIdleMontageBlendInSeconds = 1.5f;

	/** Longer blend-out for dialog listening-pool AS→AS and speak→listen (R9/R16). */
	UPROPERTY(Config, EditAnywhere, Category = "Animation", meta = (ClampMin = "0.15", ClampMax = "3.0"))
	float GodfreyDialogIdleMontageBlendOutSeconds = 1.6f;

	/** Very soft blend-in for out-of-dialog SeaIdle pool AS→AS (R13). */
	UPROPERTY(Config, EditAnywhere, Category = "Animation", meta = (ClampMin = "0.25", ClampMax = "3.0"))
	float GodfreySeaIdleMontageBlendInSeconds = 1.25f;

	/** Very soft blend-out for out-of-dialog SeaIdle pool AS→AS (R13). */
	UPROPERTY(Config, EditAnywhere, Category = "Animation", meta = (ClampMin = "0.25", ClampMax = "3.0"))
	float GodfreySeaIdleMontageBlendOutSeconds = 1.35f;

	/** Upper-body layered montage blend weight on GodfreyBodyAnimInstance (spine_01+ overlay). 1.0 so IdleStanding cannot leak into head/gaze. */
	UPROPERTY(Config, EditAnywhere, Category = "Animation", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float GodfreyUpperBodyMontageBlendWeight = 1.0f;

	/** In-dialog: clamped neck LookAt toward Exhibit_CineCamera on the body graph (Face AnimBP / ACE untouched). */
	UPROPERTY(Config, EditAnywhere, Category = "Animation|Gaze")
	bool bGodfreyConversationHeadAim = true;

	/** Max degrees the neck may rotate from the authored pose toward the cine camera. */
	UPROPERTY(Config, EditAnywhere, Category = "Animation|Gaze", meta = (ClampMin = "2.0", ClampMax = "35.0", EditCondition = "bGodfreyConversationHeadAim"))
	float GodfreyConversationHeadAimClampDegrees = 16.f;

	/** Blend weight for conversation neck LookAt. */
	UPROPERTY(Config, EditAnywhere, Category = "Animation|Gaze", meta = (ClampMin = "0.0", ClampMax = "1.0", EditCondition = "bGodfreyConversationHeadAim"))
	float GodfreyConversationHeadAimAlpha = 0.75f;

	/**
	 * Looping planted-leg clip on DefaultSlot while conversation/idle AS play on UpperBody.
	 * Stem without AS_ prefix (EyeFixed preferred at resolve).
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Animation")
	FString GodfreyPlantedStanceStem = TEXT("IdleStanding_01");

	/**
	 * Named action stems that apply root motion (actor translates with authored steps).
	 * All other library AS play in place (planted legs + upper-body overlay). Empty = none.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Animation")
	TArray<FString> GodfreyApplyRootMotionActions;

	/** When true, ExhibitionQueuePoll BeginPlay starts Godfrey Brain (node server.js) if
	 * http://localhost:3000 is not already reachable. Fixes silent PIE when Brain was not started manually.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Brain|Auto Start")
	bool bAutoStartGodfreyBrainOnBeginPlay = true;

	/** Working directory for the Brain Node process (contains server.js / package.json). */
	UPROPERTY(Config, EditAnywhere, Category = "Brain|Auto Start", meta = (EditCondition = "bAutoStartGodfreyBrainOnBeginPlay"))
	FString GodfreyBrainWorkingDirectory = TEXT("D:/Godfrey");

	/** Optional full path to node.exe. Empty = try Program Files\\nodejs, then PATH. */
	UPROPERTY(Config, EditAnywhere, Category = "Brain|Auto Start", meta = (EditCondition = "bAutoStartGodfreyBrainOnBeginPlay"))
	FString GodfreyBrainNodeExecutable;

	/** Script relative to GodfreyBrainWorkingDirectory. */
	UPROPERTY(Config, EditAnywhere, Category = "Brain|Auto Start", meta = (EditCondition = "bAutoStartGodfreyBrainOnBeginPlay"))
	FString GodfreyBrainStartScript = TEXT("server.js");

	/** Show a console window for Brain logs when Unreal launches it. */
	UPROPERTY(Config, EditAnywhere, Category = "Brain|Auto Start", meta = (EditCondition = "bAutoStartGodfreyBrainOnBeginPlay"))
	bool bGodfreyBrainShowConsoleWindow = true;

	/**
	 * If true and Unreal started Brain, terminate that process on EndPlay.
	 * Default true so PIE stop / quit cleans up the Node console.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Brain|Auto Start", meta = (EditCondition = "bAutoStartGodfreyBrainOnBeginPlay"))
	bool bStopGodfreyBrainOnEndPlay = true;

	virtual FName GetCategoryName() const override;

};

