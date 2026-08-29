# Godfrey behaviour rules (living tracker)

Owner split (agreed):

| Owner | Responsibility |
|-------|----------------|
| **UE** | Phase clocks (listen / speak / idle), blends, pools, safety (no stuck SpeakingIdle) |
| **Brain** | Meaning-driven named gestures; optional Welcome when visitor said a short hello |

Update this file whenever a rule is added or changed during testing. Status: `agreed` → `implemented` → `verified`.

---

## Rules

### R1 — Responsibility split
- **Status:** implemented (policy)
- **UE:** listen↔speak↔idle choreography, pools, blends.
- **Brain:** sparse `[gesture:…]` / mood cues; specialty AS from catalog meaning.

### R2 — GreetingWelcome deferred (no speech-path Welcome montage)
- **Status:** implemented (UE skip engage Welcome) / presence path in R17
- **Rule:** Do **not** auto-play `AS_GreetingWelcome_01` on the **engage montage path** from visitor speech.
- **Do not** use a Brain “short hello → Welcome” path for now — Brain lag would make Welcome arrive after the conversation has already moved on.
- **Reserve dedicated EngageGreet Welcome** for **visitor presence detection** — the arrival card then Welcome always run for a new Present visitor; STT must not steal that first turn — see R17.
- **UE:** `bSkipEngageGreetMontage=true` for speech-driven engage. Presence arms a one-shot Welcome via `ArmPresenceWelcomeEngage()`. First SeaIdle→dialog body hold may still draw from `DialogGreetingPool` (R15).
- **Brain:** do not emit `[gesture:GreetingWelcome_01]` (presence owns Welcome).

### R3 — Listening pool while visitor speaks / awaiting
- **Status:** implemented (superseded sequencing by R9)
- **Rule:** When listening to the visitor (or holding listen posture), pick from:
  - `ListeningAttentive_01`
  - `ListeningCurious_01`
  - `ListeningConcerned_01`
  - `ListeningNodding_01`

### R9 — Dialog idle: shuffled non-repeating AS chain + blend
- **Status:** implemented
- **Problem (log):** post-speech `AwaitReplyNeutral` section-looped one AS (`ListeningNodding_01` “set to loop”) → same clip wraps with a hard jump.
- **Rule:** In-dialog idle holds play listening-pool AS as **one-shots**, advancing through a **shuffled deck** (all stems once, then reshuffle; no immediate repeat across reshuffle).
- **Blend:** AS→AS uses soft slot replace + longer dialog idle blend (`GodfreyDialogIdleMontageBlendIn/OutSeconds`, default **1.5 / 1.6**, Hermite ease). Do **not** section-loop a single listening AS for dialog hold. **Early-chain** the next listening AS before blend-out (same as SeaIdle/SpeakingIdle) so DefaultSlot weight never drops to MetaHuman RefPose (A-pose).
- **Verify:** `[Acting] play` after EndSpeaking should cycle different `Listening*_01` stems; logs show `one-shot hold … will chain` / `reshuffled listening-pool deck`, not `section … set to loop` for AwaitReply.
- **Exception:** first hold of an encounter may use Greeting* (R15) before the listening deck.
- **Plant:** listening/greeting AS play on UpperBody over planted DefaultSlot (R20). Overlay weight is **1.0** so IdleStanding cannot leak into head/gaze (R21).

### R20 — Planted in-place body; travel is opt-in root motion
- **Status:** implemented
- **Problem (log):** speaking-pool early chain (`DescribingTheGeorgette` → `DescribingWereYouAfraid_02`) full-body lerped pelvis XY while `RootMotionMode=IgnoreRootMotion` — whole figure skated sideways under the wreck line.
- **Rule:** Conversation / listening / greeting / in-place named gestures play on **UpperBody** (`spine_01` and children, weight `GodfreyUpperBodyMontageBlendWeight`, default **1.0**). **DefaultSlot** holds a looping planted stance (`GodfreyPlantedStanceStem`, default `IdleStanding_01`) with sequence root lock. Sea idle, farewell, engage turn, and operator capture stay full-body on DefaultSlot. Named actions listed in `GodfreyApplyRootMotionActions` (and DataTable `bApplyRootMotion`) play full-body and **apply** root motion so authored steps move the actor.
- **Verify:** `[SeqStart] … slot=UpperBody travel=0` while speaking; `planted stance … on DefaultSlot`; no sideways slide on speaking-pool chain. `godfrey.PlayAction MHP_DuckUnderBanner_01` logs `travel=1` and the actor translates with the take.

### R21 — In-dialog face/eyes toward Exhibit_CineCamera
- **Status:** implemented
- **Problem:** After planted overlay, IdleStanding at weight 0.85 leaked 15% into spine/neck/head, and the cine camera sits above eyeline — face/eyes read slightly low and right of the lens.
- **Rule:** Overlay weight is **1.0** in dialog so conversation AS fully owns the head. While in dialog (`ShouldForceConversationCameraFacing`), the **body** graph applies a clamped LookAt on `neck_01` toward `Exhibit_CineCamera` (`GodfreyConversationHeadAimClampDegrees` / `GodfreyConversationHeadAimAlpha`). Do **not** install Face post-process LookAt (breaks MetaHuman head attach + ACE). Sea-idle / operator capture leave the neck alone. EyeFixed clips still supply baked eye pose.
- **Verify:** PIE log `neck LookAt on — target=(…)`; head meets the cine lens while speaking/listening; feet stay planted (`slot=UpperBody travel=0`). Tune clamp/alpha in Project Settings → Animation|Gaze if he over-turns.

### R22 — No A-pose between clips
- **Status:** implemented
- **Problem:** Slot blend-out mixed toward MetaHuman RefPose, and `Montage_Play(..., bStopAllMontages=true)` killed the planted DefaultSlot idle whenever a conversation AS started — arms/legs drifted into A-frame between (and sometimes during) clips.
- **Rule:** Body graph slot source is looping `IdleStanding` (never RefPose). In-place overlay `Montage_Play` uses `bStopAllMontages=false` so planted legs stay. Next AS plays before the previous overlay is stopped (same-slot blend). Sea-idle / travel still replace DefaultSlot as full-body.
- **Verify:** no A-pose dip at speak↔listen or speaking-pool chain; log `slot source is looping 'AS_IdleStanding_01_EyeFixed'`.

### R23 — Hands/sleeves stay outside the jacket
- **Status:** implemented
- **Problem:** Pinning Chaos Cloth to the skinned pose stopped cuff lag, but the slim coat still intersects when gestures bring hands/elbows in front of the torso or down through the hem (sleeves and body hands punch through the jacket).
- **Rule:** Body graph two-bone IK (`FAnimNode_GodfreyCoatClearance`) pushes in-front hands/elbows out to a minimum lateral clearance (`GodfreyCoatClearanceMinHandLateralCm` / `MinElbowLateralCm`). Below the chest, hands are also pushed forward of the hanging panels (`GodfreyCoatClearanceMinHemForwardCm`) so they do not come out the coat hem. Push eases on/off (`GodfreyCoatClearanceInterpSpeed`) so clip joins do not flick the sleeves. Hands behind the back, hanging at the sides, or up at the face (ThinkingHandToChin) are left alone. Sea-idle pool must not include `HandsClasped` — use `HandsBehindBack` instead. Cloth stays pinned (no Chaos sim).
- **Verify:** PIE log `GodfreyCoatClearance: pushing arms off jacket` on a describing / waist-front pose; sleeves ride outside the coat and hands stay in front of the hem; chin gestures still reach the face. Tune under Project Settings → Animation|Costume.

### R14 — Speaking body: shuffled Describing/Speaking pool (no CalmExplanation loop)
- **Status:** implemented
- **Problem (log):** whole utterance section-looped `AS_SpeakingCalmExplanation_01_EyeFixed` at rate 0.55 → repetitive underuse of library.
- **Rule:** While speaking, play body AS from `SpeakingIdlePool` as **one-shots** in a shuffled non-repeating deck (soft blend AS→AS). Default pool: `SpeakingCalmExplanation` / `SpeakingGentleEmphasis` / `SpeakingDescribe*` / `SpeakingExplainDanger` / all `Describing*`. Never section-loop one speaking clip for the utterance. Conversation overlays (speaking pool, listening, in-place named gestures) use the **dialog** blend times (`GodfreyDialogIdleMontageBlendIn/OutSeconds`, default **1.5 / 1.6**), not the short 0.45s body blend — otherwise each take’s start pose snaps in. Blend option is HermiteCubic. Travel / farewell / sea-idle keep their own profiles.
- **Verify:** `[Acting] play … context=SpeakingIdle` cycles different Describing/Speaking stems within one utterance; logs show `speaking pool next` / `reshuffled speaking-pool deck`, not repeated CalmExplanation-only. `[SeqStart] … slot=UpperBody … blendIn=1.50 blendOut=1.60` on speaking/listening/gesture overlays.

### R15 — First dialog hold uses Greeting* once
- **Status:** implemented
- **Rule:** When Godfrey first transitions SeaIdle → in-dialog, the **first** await/listen body hold picks from `DialogGreetingPool` (`GreetingNod` / `GreetingSmallSmile` / `GreetingHaveASeat` / `GreetingWelcome`). Subsequent dialog holds use the listening pool (R9). Reset on return to SeaIdle.
- **Exclude:** `GreetingTurnToVisitor` (camera already frontal — see `bSkipEngageTurnMontage`).
- **Verify:** first `[Acting] play` after engage shows a `Greeting*` sequence; later holds show `Listening*`. Do not replace Greeting mid-clip with ConversingIdle Listening (hold flag).

### R16 — Post-speech speaking body hold + soft speak→listen blend
- **Status:** implemented
- **Problem (log):** `NotifyUtteranceEnded` → `StopSpeaking` + `AwaitReplyNeutral` Listening* on the **same frame** → abrupt body cut while lipsync correctly stops.
- **Rule:** After audible speech ends, keep the current speaking body AS for `PostSpeechSpeakingHoldSeconds` (default **1.0s**). Lipsync/audio already stopped. Then soft-blend into Listening* using longer dialog blends (`GodfreyDialogIdleMontageBlendIn/OutSeconds`, default **1.5 / 1.6**). Cancel the hold immediately if a new utterance starts.
- **Also:** mid-utterance speaking-pool advances use early soft chain (timeline/rate wall) so short clips do not hard-cut. Chain lead and blend-in/out are the same dialog times so the next take does not win in 0.45s.
- **Verify:** logs show `[PostSpeech]: holding speaking body` then later `soft-blend speaking body -> Listening*`; Listening* `[Acting] play` is ~1s+ after `Speech Finished`.

### R4 — Listening ends when Godfrey speaks
- **Status:** implemented (existing `HandleSpeakingStarted` stops body montages then SpeakingIdle)
- **Rule:** As soon as Godfrey audible speech starts (`OnAnimationStarted` → BeginSpeaking), Listening montage must stop and blend into a speaking sequence.

### R5 — No stale Speaking after engage greets
- **Status:** implemented (2026-08-02: fixed inverted engage-complete drop)
- **Rule:** If utterance ends while engage is still in progress, clear pending Speaking (`NotifyUtteranceEnded` / `EndSpeaking`) so engage-complete cannot start SpeakingIdle after audio is done.
- **Also:** if pending Speaking is still set when engage finishes, **promote** it (`ApplyPendingPostEngageState` → `EnterSpeaking`). Do **not** drop pending Speaking merely because `PerformanceState != Speaking` — that is the normal deferred path when speech starts engage from SeaIdle (especially with EngageTurn/Greet skipped). Dropping it leaves Listening body for the whole utterance (occasion / queue speech bug).
- **Verify:** after Queue occasion from SeaIdle, logs show `BeginSpeaking — sync engage from SeaIdle; EnterSpeaking now` (and/or `engage finished — Conversing (pendingSpeak=1…)`), then speaking-pool `[Acting] play` with **rate≈0.90** — **not** `dropping stale pending Speaking`, **not** listening-pool `AwaitReplyNeutral`, and **not** speaking-pool `rate=0.55` (legacy half-speed). Rebuild required after C++ change.

### R6 — Catalog / cues
- **Status:** implemented (existing)
- Brain mirror: `D:/Godfrey/config/godfrey-performance-action-catalog.json`
- UE list: `Config/GodfreyPerformanceActionCatalog.json`
- Cue contract: `Docs/GodfreyPerformanceCueContract.md`

### R7 — In-dialog vs out-of-dialog (animation partition)
- **Status:** implemented
- **Meaning:**
  - **Out of dialog:** game start (SeaIdle), and after farewell AS has started / completed (back to sea).
  - **In dialog:** from the moment a visitor has spoken (engage) until farewell begins.
- **API:** `UGodfreyPerformanceStateComponent::IsInDialog()` — true for Engaging + Conversing.
- **Idle rule while in dialog:** after the optional first Greeting* hold (R15), use attentive listening-pool AS (`ListeningAttentive` / `Curious` / `Concerned` / `Nodding`). Do **not** use looking-out-to-sea idles (`IdleLookingToSea_*`, SeaIdle exhibition hold) while in dialog.
- **Out of dialog:** SeaIdle exhibition hold is allowed (and is the default). Sequencing is R13 — not a single section-looped AS.

### R13 — Out-of-dialog SeaIdle: shuffled non-repeating AS chain + soft blend
- **Status:** implemented
- **Problem (log):** exhibition `SeaIdle` section-looped one AS (`IdleLookingToSea_01` “set to loop”) → hard wrap every ~16s wall-clock; beard/face snap.
- **Rule:** Out-of-dialog idle plays a **shuffled deck** of calm exhibition AS as **one-shots**, soft-blending AS→AS. Never section-loop a single SeaIdle clip.
- **Default pool:** `IdleLookingToSea_01/02`, `IdleStanding_01`, `IdleWeightShift_01`, `IdleRockingOnFeet_01`, `HandsBehindBack_01` (no `HandsClasped` — R23 jacket clearance). Override via `SeaIdleExhibitionPool`. EyeFixed preferred.
- **Blend:** `GodfreySeaIdleMontageBlendIn/OutSeconds` (default ~1.25 / 1.35). Soft slot replace — do not hard-stop the previous montage.
- **Early chain:** start the next AS ~blend-out seconds *before* the current ends (`early chain in …s — overlap avoids empty slot). Chaining only on `MontageEnded` used to empty the slot; Body AnimBP source is now looping **IdleStanding** (not MetaHuman RefPose), so a missed chain cannot A-pose.
- **Verify:** logs show `reshuffled sea-idle deck`, `sea-idle pool next '…'`, `early chain in …s`, `sea idle advance … (soft crossfade)` — not `section … set to loop`, and not frequent `ended without early chain — advancing (RefPose risk)`.

### R20 — Planted in-place acting vs authored travel
- **Status:** implemented
- **Problem:** speaking-pool (and other) full-body `DefaultSlot` blends interpolated pelvis XY between unrelated takes → whole body skated sideways with no steps. `IgnoreRootMotion` stopped the *actor* moving but left mesh root/pelvis translation in the pose.
- **Rule:** Conversation, listening, greeting, and sea-idle pool AS play **in place**. Legs stay on a looping planted stance (`IdleStanding_01` on `DefaultSlot`). Performance plays on `UpperBody` (layered from `spine_01` up). Soft AS→AS chain stays on UpperBody so feet cannot lerp. Named actions listed in `GodfreyApplyRootMotionActions` (and DataTable `bApplyRootMotion`) play full-body on `DefaultSlot` with **root motion applied** so a real step moves him. All other named gestures stay in-place overlays.
- **Verify:** `[SeqStart] … slot=UpperBody travel=0` during speech; `planted stance 'AS_IdleStanding_01_EyeFixed' on DefaultSlot`; owner location does not drift mid-utterance. Travel logs `slot=DefaultSlot travel=1`.

### R8 — End Speaking with audible audio (not FinishStream)
- **Status:** implemented
- **Rule:** Speaking body AS must stop when ACE audible playback ends. Audio-end watchdog arms on `OnAnimationStarted` (not only after `FinishStream`), so late HTTP ingest cannot leave SpeakingIdle looping after speech has stopped. ACE face curves must stop with the voice (`AceComp->Stop()` on playback-complete) — do not leave flushed blendshapes running after audio.

### R10 — Idle re-engagement is game-owned (not Brain)
- **Status:** implemented (conversational nudge)
- **Rule:** After visitor inactivity, any “keep them engaged” prompt must be initiated by **Unreal**, not the Brain web UI or Brain timers.
- **Brain web:** idle nudge (`scheduleIdleNudge` / `speakAssistantReply` after quiet) is **disabled**.
- **Brain replies:** closing questions *inside* a solicited answer (system-prompt invitations) are still allowed — that is not idle re-engagement.
- **UE:** While `Conversing` + webcam Present (or no presence component), after `DialogEngageSilenceSeconds` (default **16s**) of **Speak-green** silence, UE `AskGodfrey(DialogEngagePrompt)` — continue from session history; do not re-ask a name already known or already asked; no goodbye. Do **not** fire if visitor speech is already in progress (`speech_started` / awaiting transcript). Do **not** fire a second nudge while the first is still in flight (Thinking / HTTP / audible). If a nudge is already in flight (Thinking / HTTP) and the visitor starts speaking, abort that stream immediately — it does not count as an unanswered attempt. Timer **ends** when anyone speaks — visitor **`speech_started`** (not only STT final) or Godfrey utterance start/end — and **restarts** on the next silent period (`speech_stopped`, or Speak green after Godfrey). After **`DialogEngageMaxUnansweredAttempts`** (default **2**) of those nudges with no visitor speech, Unreal assumes they have left: `BeginFarewell` → SeaIdle. Then wait **`AbandonedEmptyRecaptureDelaySeconds`** (default **2s**) and recapture the empty-room webcam baseline (`unanswered-idle`) so a vacant room is not stuck as Present. Presence Welcome stays blocked until that recapture sets Empty; a later real arrival can Welcome. Prevents engagement from stealing a turn while STT final is still in flight. While awaiting a final after `speech_stopped` (or on `transcript_missed`), R18 owns the turn instead. The ACE audio-end watchdog must not treat a mid-reply TTS gap as end-of-speech (`Finished=0`); a false `EndSpeaking` opens Speak-green and lets this prompt steal the rest of the answer. `VisitorIdleTimeoutSeconds` (60s) still → farewell / SeaIdle if nudges are off.
- **Verify:** after speech: Speak green ≥16s → `dialog engagement AskGodfrey unanswered=1/2`; start talking during the wait-for-Brain silence → `aborting dialog engagement prompt (visitor speech_started)` and Godfrey does **not** speak the nudge. Another quiet window → `unanswered=2/2`; a third quiet window → `assuming visitor left` + `BeginFarewell` + SeaIdle, then `SeaIdle — recapturing empty background in 2.0s` and `empty background capture started (unanswered-idle)`. A later walk-in after that recapture must Welcome. Visitor speech resets the count.

### R18 — Missed STT → please repeat
- **Status:** implemented
- **Rule:** If the visitor’s mic turn is detected (`speech_started` → `speech_stopped`) but no usable `transcript_completed` arrives (empty/echo from Brain `transcript_missed`, too-short final, or timeout), Unreal must not stay silent and must not fall through to R10 engagement as if they never spoke.
- **UE:** `UGodfreyVoiceInputComponent` arms an await on `speech_started`; after `speech_stopped` waits `MissedTranscriptTimeoutSeconds` (default **2s**). On miss → `AskGodfrey(MissedTranscriptPrompt)` so Godfrey apologises and asks them to repeat. Clears the await on a good final. Engagement (R10) stays blocked while `IsAwaitingVisitorTranscript()`.
- **Brain STT:** empty / prompt-echo completions emit `transcript_missed` (not a silent drop). One-word noise hallucinations (`cool`, `hello`, `thanks`, …) are dropped as `transcript_missed` reason `hallucination` and Unreal must **not** fire R18 please-repeat (nobody spoke). **Roleplay hallucinations** (multi-speaker `Tom:` / `Visitor:` scripts invented from the STT prompt) are dropped the same way. Server VAD silence is **1500ms** (`GODFREY_UNREAL_STT_SILENCE_MS`); VAD threshold default **0.50** so room tone is less likely to start a fake turn. Unreal keeps the mic paused `GodfreyPostSpeechIgnoreSeconds` (default **1.25s**) after audible end, arms that ignore **before** unpausing (no one-frame speaker-tail leak), and drops the same hallucination list if a fake final still arrives. Real first names are kept.
- **Lantern / turn commit:** Speak stays green until the visitor turn is committed. Do not `AskGodfrey` on `transcript_completed` while `speech_started` is still in progress, on a transcript with no `speech_started` since mic resume, or before `VisitorTurnCommitDelaySeconds` (default **0.5s**) after `speech_stopped` — that delay lets a mid-sentence continuation cancel the commit (lantern stays Speak). Orphan `speech_stopped` after resume (OpenAI VAD ending a cleared buffer) is ignored. Empty exhibition-queue status polls must not pause the mic (`IsQueuedSpeechPlaying`, not `IsStreamActive`). ACE trailing hush after playback-complete must not re-open Speaking (`OnAnimationStarted` ignored once the utterance has ended).
- **Verify:** sharp one-syllable name with no final → log `missed transcript → AskGodfrey please-repeat` → Godfrey says sorry / please repeat; a clear longer phrase still gets a normal transcript reply. Speaking through a pause must log `holding transcript while visitor still speaking` / `commit delay` and must **not** flip Wait until they have finished. A `Tom:`/`Visitor:` script in the log must be `dropping STT roleplay-hallucination`, not `AskGodfrey transcript=`.

### R12 — Visitor goodbye: Brain flags, Unreal decides when
- **Status:** implemented
- **Rule:** The Brain reports that the visit is over; **Unreal owns the farewell transition** and only starts it once the goodbye reply has finished speaking. Same split as R10.
- **Brain:** `lib/conversation-end.js` sets `conversationEnd` from two signals — a farewell phrase in the visitor's own last message (short, non-question: "goodbye", "I've got to go", "I must be off", …) or a `[farewell]` / `[goodbye]` cue in the reply. On the **queued** path this is served on `/api/exhibition/unreal-tts-status`. On the **exhibition mic** path (`POST /api/godfrey/speak/stream-pcm`) it is sent as `X-Godfrey-Conversation-End` on the PCM response, and the prompt is told to bid goodbye (one or two sentences, end with `[farewell]`) instead of continuing the story.
- **UE:** `TryLatchConversationEndFromStatusJson` / `TryLatchConversationEndFromStreamHeaders` → `RequestConversationEnd`. The latch keeps Godfrey **in dialog** (R7); `BeginFarewell` runs from `NotifyUtteranceEnded`. A farewell **cue** latches the same way — it must never call `BeginFarewell` directly, because cues arrive with the queued reply before its audio streams. VoiceInput does not drop a goodbye transcript while Godfrey is still speaking — it queues it and sends it when that line finishes, so R10 cannot steal the turn.
- **Cancel:** a new visitor question (`NotifyReplyIncoming`) drops the latch.
- **Fallback:** `ConversationEndFallbackSeconds` (15s, restarted while Speaking / awaiting Brain) runs the farewell if the reply never streams.
- **Verify:** say "I've got to go now, goodbye" on the Speak-green mic → Brain log `visitor farewell`; Unreal `conversation end latched (brain conversationEnd (visitor_phrase))` then `BeginFarewell` after `[Speech] Speech Finished`. He must not continue the story or fire a dialog engagement prompt. `"It's good to see you back, Godfrey."` must **not** latch farewell (`see you` greeting, not a leave). `node scripts/check-conversation-end.js`.

### R11 — Speech loudness is set in Unreal, not by the ACE component
- **Status:** implemented
- **Problem (log):** Brain TTS PCM arrives at ~`streamPeak` 7000–10600 and `streamRms` 900–1500 of int16 full scale (≈ −10 dBFS peak / −28 dBFS RMS) — audible but far too quiet for the exhibition. Mixer, `PrimaryVol`, `AppMult` and ACE `Volume` were all 1.000, so nothing downstream was attenuating.
- **Rule:** Godfrey speech level is raised in `PushPcm16Chunk` by `GodfreySpeechPcmGain` (Project Settings → Plugins → Unreal Performer (Godfrey / ACE) → Audio, default 2.5) before PCM reaches ACE. Peaks above 85% of full scale are soft-saturated, so louder TTS clips gracefully instead of hard-clipping.
- **Why not ACE `Volume`:** `UACEAudioCurveSourceComponent::Volume` is clamped 0–1 and is only copied to the procedural wave when the wave is created / playback starts, so it can neither add gain nor change level mid-utterance.
- **Verify:** `[Godfrey audible diag] … streamPeak=… streamRms=… (post-gain x2.50, saturated=N gated=M gate=750 …)`; `saturated` should stay a small fraction of samples. Raise/lower the gain in Project Settings — it applies to the next chunk, no restart needed. Lip-sync amplitude is compensated separately (`GodfreyAceInputStrength` auto `1/gain`) so louder speech does not over-drive the mouth.

---

### R17 — Webcam visitor presence (first slice)
- **Status:** implemented (motion-gated adaptive occupancy; not a person NN yet)
- **Hardware:** USB webcam via Media Framework (`WmfMedia`) + `UGodfreyVisitorPresenceComponent` (auto-spawned from ExhibitionQueuePoll when enabled).
- **Sense states:** `Empty` → `Approaching` (enter dwell) → `Present` → `Leaving` (leave dwell). Distinct from exhibition `SeaIdle/Engaging/Conversing/Farewell`.
- **Detection:** illumination-normalized luminance vs adaptive empty background in a UV zone, **plus frame-to-frame motion**. Coarse blob count → `EstimatedVisitorCount` (0/1/2+). Occupancy is not a person NN.
- **Enter:** `Empty` → `Approaching` on appearance occupancy; `Present` / Welcome only if ~**0.8s** of motion accumulated during enter dwell (default **1.75s**). A moved chair or lighting patch is a static difference and is absorbed as the new empty room instead of Welcome.
- **Empty baseline:** startup ignore then average `EmptyBackgroundCaptureFrames`. Recapture after leave (~0.75s), after **15s** stillness with high occupancy and no motion, every **180s** while Empty, and **2s after SeaIdle** when R10 unanswered-leave assumed they left (`unanswered-idle`) so a vacant room is not stuck Present. While Present, only unchanged pixels slow-learn lighting; the visitor blob stays frozen. Operator **F11** recaptures now.
- **Startup:** ignore the first `StartupIgnoreSeconds` (default **2s**) after the webcam is live so the operator can leave the frame; then capture empty. Preview shows `IGNORE …` then `EMPTY CAPTURE …`.
- **Engage:** when `Present`, play the **arrival briefing card** then Welcome — **even if the mic already heard something**. STT must not `AskGodfrey` until that Welcome has been delivered this occupancy (Empty / Approaching / card-on-screen). Then arm Welcome montage + `NotifyVisitorEngaged()` (R2), then UE-owned **welcome speak** via `AskGodfrey(WelcomeSpeakPrompt)` (~0.35s delay so greet starts first; Thinking/Speaking defer until Engaging finishes). Speech-only exhibits (webcam off) still engage from the mic as before.
- **Arrival briefing:** Constantia card + dim veil (`UGodfreyVisitorBriefingComponent`, auto-spawned with webcam presence). Copy: the way is far; GREEN = only one may speak, clearly into the microphone; RED = he cannot hear you; the way is not swift. Mic is held for the whole card (lantern stays Wait). Overlay Z is below the brass lantern so Speak/Wait remains visible. Fade in 0.8s, hold 12s, fade out 1.8s, then Welcome. Empty during the card cancels it (no Welcome). Next visitor after Empty sees it again. Operator **F8** skips. Project Settings → Vision|Presence (`bGodfreyEnableVisitorBriefing`). Godfrey does not speak this copy.
- **Leave:** freeze visitor pixels while Present. Occupancy uses **hysteresis**: enter at **0.06**, leave only below **0.03**, plus **leave dwell default 2s** of continuous emptiness, then `Present → Leaving → Empty` while in dialog → `RequestConversationEnd(webcam visitor left)` + goodbye `AskGodfrey(FarewellSpeakPrompt)` (name from Brain visitor profile when known; prompt asks for `[farewell]`). Wave runs after the line via R12 — do **not** `BeginFarewell` mid-sentence. Then recapture empty, but **skip recapture** if occupancy is still above the leave threshold (do not bake a standing visitor into the empty model).
- **Activity:** while Present/Leaving and in dialog, periodically `NotifyVisitorActivity()` so silent standing does not hit the 60s idle farewell.
- **Debug:** webcam preview off by default (F9 toggles); force occupied (F10); recapture empty (F11); 5s occupancy heartbeat while Present/Leaving (occ/mot/emptyAge). Top-right brass **signal lantern** Speak/Wait cue (Constantia labels) synced to mic accept window. Mic stays paused for the full ACE audible utterance (not only HTTP `IsStreaming`); Speak opens **~1.25s** after playback complete. Project Settings → Vision|Presence / Listen Cue. Track/format enumeration logs a one-line summary; per-format dump is `LogGodfreyVision` Verbose only (WMF webcams can expose 200+ formats).
- **Verify:** walk in (mic quiet or not) → arrival briefing card (log `holding Welcome for arrival briefing`) then `arrival briefing finished` → Welcome speak. A stray STT word before Welcome must log `holding STT until arrival briefing/Welcome` and not skip the card. Walk out and stay out ~2s → `Present → Leaving → Empty`, `absence farewell latched`, `farewell speak AskGodfrey ok=1`, then goodbye + farewell wave, then empty recapture (`leave`). A brief occupancy dip while still standing must **not** farewell. Move a chair while empty → Approaching without Welcome, then `static-occupancy` or `stillness` recapture. F11 recaptures immediately. After he finishes a line, a lone STT `Cool.` / `Hello.` must log `dropping STT noise-hallucination` and not `AskGodfrey`.

### R19 — Notable visitor recognition (Brain)
- **Status:** implemented
- **Owner:** Brain (visitor profile + occasion). Unreal unchanged — normal speak / TTS path.
- **Rule:** A small watchlist (`D:/Godfrey/config/notable-visitors.json`) may identify a known visitor from what they say, not from the webcam. Given name on the list → ask family name once. Confirmed surname (with STT aliases) → one verbatim occasion speech, then remember them for the rest of the encounter. Prefer a missed identification over greeting the wrong person. Do not discover them twice. Godfrey remains in 1877: no years, doctorates, or modern book titles.
- **Marcia van Zeller:** occasion `marcia-van-zeller`. Operator fallback: Admin → Occasion scripts (if the mic misses her name).
- **Stef (Stefanie) Koens:** occasion `stef-koens`. Word has reached him that she likes to write stories about sunken ships. He does not know the Batavia, years, or a book of hers. Address her as Stef.
- **Not on the watchlist:** John / Pancake John — John is too common; every John would be asked for a surname. Occasion `john-sullivan` remains for Admin queue only.
- **Encounter memory:** Brain keeps the name (and recent turns) until farewell / webcam leave. Idle reset is a ten-minute safety net — it must not wipe the card during a long spoken reply. A known name is a hard instruction: never ask who he is speaking with.
- **Verify:** say "Marcia" → he asks family name; "van Zeller" → authored recognition once; later turns address her as Marcia without repeating it. "Do you know Marcia van Zeller?" does not trigger. After a long wreck telling, a sympathy line must still use the given name (not “who am I speaking with?”). Same pattern for Stef / Koens. A plain "John" must not prompt for a watchlist surname. `node scripts/check-visitor-profile.js`.

## Pending / not yet specified

- Stronger person detector (replace adaptive differencing) / group addressing copy
- ~~UE-owned idle engagement prompt (R10)~~ — **done** (3s Present+dialog conversational nudge)
- ~~Presence-without-speech Welcome~~ — **done (R17 first slice)**
- ~~Speaking idle pool~~ — **done (R14)**
- ~~Brain-side “short hello → Welcome”~~ — **rejected** (too much lag; use presence)

---

## How we work

1. You state or amend a rule during testing.
2. This file is updated (id, owner, status).
3. Code / Brain prompt / catalog are changed to match.
4. Verify with `[Acting]` logs (`context=` + `sequence=`).
5. Keep **in/out of dialog** (R7) in mind for any new idle / listening / sea AS behaviour.