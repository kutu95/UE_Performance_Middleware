# Godfrey speech pipeline: ACE / Audio2Face (living tracker)

How Godfrey's audio reaches Audio2Face, why the tuning values are what they are, and
which theories have already been tested and disproven.

Companion to `GodfreyBehaviourRules.md` (that file owns choreography; this one owns the
audio/curve path). Brain-side latency work is documented in `D:/Godfrey/SPEECH_PIPELINE.md`.

Update this file whenever a value in the ACE ingest block changes or a new theory is
tested. Status: `measured` → `implemented` → `verified`.

---

## The path

1. Brain streams PCM16 LE mono @ 24 kHz over HTTP (`POST /api/godfrey/speak/stream-pcm`).
2. `AsyncActionStreamGodfreySpeech` buffers the body into `GodfreyPcmStreamSession`.
3. The session pushes the buffer into ACE via `AnimateFromAudioSamples`, capped at
   `AceMaxPcmPushChunkDurationMs` per call.
4. A2F (local provider `LocalA2F-Mark`) produces blendshape curves **and** drives the
   audible procedural playback. Its throughput therefore gates both lip sync and audio.
5. When the HTTP body has drained, `EndAudioSamples` signals end-of-stream so A2F emits
   the curves it has been holding back.
6. An audio-end watchdog in the session ends the utterance, because ACE's
   `OnAnimationEnded` does not reliably fire on this path.

---

## Measured constants

All figures from the 2026-08-01 test runs, RTX-class GPU, editor PIE at ~120 FPS.

| Property | Value | Notes |
|---|---|---|
| A2F curve throughput while streaming | **3.5–3.8x real time** | Only when ingest is *not* throttled |
| A2F curve throughput during flush | **4.7–5.0x real time** | Same order as streaming; no mystery gap |
| Curves withheld until end-of-stream | **~0.78–0.88s** | Hard floor. A2F will not emit these before `EndAudioSamples` |
| `EndAudioSamples` cost | **≈ backlog ÷ 4.8** | 1.49s→317ms, 1.43s→307ms, 1.33s→267ms, 0.88s→163ms, 0.78s→167ms |
| Practical flush floor | **~165ms** | Because the backlog cannot fall below the withheld ~0.8s |
| Tail accuracy at watchdog | **0.023–0.030s unmatched** | Against a 0.03s safety margin; no audio clipped |

**The withholding floor is the key constraint.** A2F deliberately holds back the last
~0.8s of curves until end-of-stream is signalled. This is what makes the flush
unavoidable, and it is why the deferral threshold cannot usefully go below ~0.9s: the
wait would simply time out. Verified directly by setting the threshold to 0.2s, at which
point both utterances plateaued at 0.877s and 0.777s and hit the catch-up timeout.

---

## Load-bearing config

`Config/DefaultEngine.ini`, section `[/Script/UnrealPerformer.UnrealPerformerGodfreySettings]`.
These look arbitrary and are not. Do not "tidy" them without re-reading this file.

| Setting | Value | Why |
|---|---|---|
| `bGodfreyAcePaceIngestByCurveCatchUp` | `False` | **Critical.** When `True`, Unreal throttles pushes whenever curves fall behind. That starves A2F, which then emits digital silence — a measured 938ms run of exact zeros mid-speech. This flag was the original audio dropout. |
| `bGodfreyDeferEndAudioSamplesForCurveCatchUp` | `True` | With deferral off, `EndAudioSamples` fires at HTTP drain with a 11–21s backlog and blocks the game thread for 2–4 seconds at the start of speech. Deferral lets the backlog shrink first. Do **not** treat A2F’s ~0.9s unmatched floor as “caught up” while the playhead is still mid-utterance (Welcome HTTP burst used to hitch the intro). |
| `GodfreyAceEndAudioMaxUnmatchedSeconds` | `0.9` | Just above the ~0.88s withholding floor, so the wait ends naturally rather than by timeout. Lower values are unreachable; higher values cost a proportionally longer flush. |
| `GodfreyAceEndAudioCatchUpTimeoutSeconds` | `3` | Legacy short timeout. **Long utterances** no longer force `EndAudioSamples` after 3s while unmatched is still large mid-speech (that caused multi-second game-thread freezes on occasion speeches). UE waits for natural catch-up, then **flushes once Wall has played all sent PCM** (any length). 120s is only a stall deadline. |
| `GodfreyAceIngestStallTimeoutSeconds` | `6.0` | After playback has caught sent PCM, if HTTP is still open and no new samples **or HTTP body bytes** arrive, Unreal FinishStreams. Prevents silent lip-sync when Brain/TTS hangs. Must stay above a pipelined ElevenLabs flush of the closing sentence (2.5s clipped the tail). |
| `AceMaxPcmPushChunkDurationMs` | `55.0` | Audio per `AnimateFromAudioSamples` call. |
| `[/Script/ACECore.ACESettings] BurstMode` | `ForceBurstMode` | See dead ends. Real-time mode makes `AnimateFromAudioSamples` blocking and collapses the editor to 3 FPS. |
| `bApplyAceBurstInferenceOverrideAtStartup` | `True` | Applies the above at startup. |

---

## Third-party plugin patch (at risk on plugin update)

`Plugins/NV_ACE_Reference/Source/A2FLocal/Private/A2FLocal.cpp`, around lines 298–315.

Upstream NVIDIA code writes the **Audio2Emotion** VRAM budget into
`A2FCommonCreationParams`, so Audio2Face runs on the much smaller emotion budget and its
TensorRT workspace is starved. Our copy assigns `Budget.A2F3D` to A2F and `Budget.A2E` to
A2E, as intended.

The fix is correct but was **not** the performance bottleneck we were chasing — A2F
throughput was unchanged afterwards. Keep it anyway; it is a genuine upstream bug. There
is an explanatory comment in the source at the patch site.

**Any update to `NV_ACE_Reference` will silently revert this.** Re-apply and re-test.

---

## Source fixes worth understanding

Both look like odd code without the story behind them.

**Watchdog handling of `Wall == -1`** — `GodfreyPcmStreamSession.cpp` (~line 1867).
When ACE procedural audio stops, `GetPlaybackWallSeconds` returns -1. The
`bProceduralStoppedNearEnd` condition was gated on `Wall > 0.35`, so it could never fire
and Godfrey stayed stuck in a silent speaking state indefinitely. We now track
`LastPositiveProceduralWallSeconds` and evaluate against an `EffectiveWall`.

**Watchdog must not end while HTTP is still ingesting** — 2026-08-17 Marcia: "Tell me
something you think I wouldn't know." Watchdog fired at Wall=1.10s / Expected=1.04s /
Finished=0 (CaughtMax+CaughtQuiet+WallPastSamples) during a TTS gap after the first
second. Remaining PCM was discarded (`bAcePlaybackEndedObserved`), then R10 idle
engagement stole the turn. Completion now requires `FinishStream` (`bFinished`) first.
A mid-reply pause is silence, not end-of-speech.

**Ingest stall after audio dies (standalone 2026-08-20 utt-8)** — visitor "No, I've never
been out to sea." Brain/TTS hung after ~6.5s of PCM (last heard: "how it feels"). HTTP never
completed, so the watchdog still refused to end (`bFinished=0`). ACE audio stopped; speaking
body AS chained to a 32s describing montage, which looked like continued lip-sync. After
playback has caught sent PCM, if no further samples arrive for
`GodfreyAceIngestStallTimeoutSeconds` (6s), Unreal cancels the hung POST and FinishStream.
Brain pipeline also aborts an idle LLM stream after 8s and flushes TTS.

**Short-reply EndAudioSamples deadlock** — 2026-08-19 Marcia given-name turn (`utt-23`,
Sent=3.4s, unmatched stuck at 0.947s vs 0.9s gate). Deferral waited for the watchdog to
mark playback ended before calling `EndAudioSamples`, but the watchdog requires
`FinishStream` (`bFinished`) first. Force-flush also required `Sent > 5s`, so a surname
ask never flushed: audio died after a few words, visemes kept running, lantern stayed
Wait until PIE stop. Flush as soon as Wall has played the sent PCM.

**Deferred `SetReadyToDestroy()`** — `AsyncActionStreamGodfreySpeech.cpp` (~line 1589).
In direct speech mode the action was released at HTTP-complete, dropping the last
reference to `StreamSession`. Garbage collection then cancelled the end-of-playback
watchdog mid-utterance, which is why the hard timeout never fired. The action is now held
alive via `bAwaitingPlaybackBeforeDestroy` until ACE playback ends.

**PIE stop / restart freeze** — stopping mid-utterance used to leave an unmatched A2F
session. The next PIE then blocked in `LocalA2F-Mark` alloc / `PrepareNewAudioComponent`
(game thread stuck ~90s). EndPlay now cancels HTTP, mutes/`Stop()`s ACE playback, then
calls `EndAudioSamples` so `ActiveA2XSessions` is cleared (`AbortActiveStreamForCharacter`).
Stop may hitch for the unmatched backlog; that is cheaper than a ~90s freeze on restart.
Webcam on-open no longer dumps 200+ WMF formats at Log.

**Tail safety margin** — `SentAudioTailMarginSec = 0.03f`.
The watchdog previously allowed muting up to 80ms early, clipping the last word. All
completion conditions now require `Wall >= ExpectedFromSamples + 0.03s`. Last-voice
viseme rest no longer `Stop()`s ACE 0.18s early (that clipped “Thank you, Fred”).

**Lip-sync after audio** — muting `ACE Volume` on playback-complete left blendshapes
running (A2F flush / unmatched tail). Playback-complete now `Stop()`s the curve source
and the watchdog ends as soon as procedural audio is silent near the sent duration,
without waiting for leftover curves.

**Stop lips with the voice** — 2026-08-17 after "what do you look for, in that place":
A2F still applied visemes through trailing TTS hush, then Apply ACE independently
decayed 55 blendshapes to zero (chewing after speech). The 21:50 Marcia close
("held her on for the coast as I did?") still twitched: PIE was on a stale
UnrealPerformer binary (watchdog log had no `LastVoice` / `PastLastVoice`; diag had
no `gated=`), so ACE ran visemes through the full PCM duration, then `Stop()` snapped
the last viseme of "did?" to rest.

22:16 "Might I know your name?": rest *did* fire (`LastVoice=12.858`) but ACE was left
running until `ExpectedSamples=13.180` (~300ms of hush visemes). Playback-complete now
`Stop()`s at last voice.

22:25 "stood out in weather yourself?": `Stop()` did fire at last-voice
(`LastVoice=20.642` vs `Expected=21.080`) and the face still chewed. The silence gate
(750) is far too low to mark end-of-speech: decaying breath/fricative still updates
LastVoice, and A2F lip-syncs that tail.

22:41 "do you wish to know what happened at sea?": RMS LastVoice (9.900) was only
101ms before the gate (10.001). Question-rise visemes plus body-AS `CTRL_expressions_jaw*`
on the 1s post-speech speaking hold still moved the mouth. Rest now starts **180ms before**
the last strong RMS window, floor is ~16% of peak (min 4500), and Apply ACE forces
MetaHuman jaw/mouth CTRL curves to 0 while rest is latched. Confirm: `LastVoice` clearly
less than `LastVoiceGate`, rest Wall before LastVoice.

**Over-articulation / lips during pauses (2026-08-17 Marcia occasion)** — exhibition
`GodfreySpeechPcmGain` (x2.5) is applied *before* ACE ingest, so A2F sees hotter audio
than native TTS and treats pause noise as speech. Compensations: `inputStrength` auto
`1/gain` (0.4 at x2.5), `lipOpenOffset=-0.03` so rest frames stay closed, and a post-gain
silence gate (`GodfreySpeechSilenceGate=750`) that zeros near-silence before A2F. Heard
level is unchanged. Tune in Project Settings → Audio | Lip Sync.

**PIE stop AV in `FailAndStop`** — 2026-08-11: stopping PIE mid-`stream-pcm` cancelled
the request, then HTTP `OnProcessRequestComplete` still ran with a raw `this` after the
async action was destroyed (`EXCEPTION_ACCESS_VIOLATION` at `FScopeLock(&HttpBodyLock)`).
Progress/complete now use `TWeakObjectPtr`; Cancel/FailAndStop unbind before cancel.

---

## Dead ends (do not re-attempt without new information)

| Theory | Result |
|---|---|
| A2F is slower than real time (~0.98x) | **False.** That figure was our own ingest throttle. A2F runs 3.5–5x when unthrottled. |
| A2F VRAM budget bug is the bottleneck | Bug was real and fixed, but throughput was unchanged (0.85x / 1.26x / 0.96x after the fix). |
| Switch A2F to `ForceRealTimeMode` | Catastrophic. `AnimateFromAudioSamples` becomes blocking; editor drops to **3 FPS**. Incompatible with this ingest architecture. |
| GPU contention from rendering | Capping editor FPS (`t.MaxFPS 60`) changed A2F throughput not at all. |
| Chain CIG `CudaParameters` onto A2F/A2E sub-instances | **Crashes.** `0xc0000409` in `nvinfer_10.dll` (TensorRT). Reverted. The CIG context is created but never reaches `createInstance` (`chooseCudaContext` warning). |
| Increase the ingest push budget floor to 2 | No effect. The Brain delivers at ~9x real time; starvation was never on the delivery side. |
| The end-of-speech glitch is the `EndAudioSamples` hitch | Only partly. The audible dropout was curve starvation; the hitch is a separate, smaller problem. |
| Defer `EndAudioSamples` until curves fully catch up | **Circular.** A2F withholds the tail curves *until* end-of-stream, so the wait can never succeed. Caused ~0.9s of audio with frozen lip sync at the end of every speak. |

---

## Diagnosing a regression

Log file: `Saved/Logs/UnrealPerformer.log`.

```powershell
# Flush cost, deferral behaviour and end-of-utterance accuracy
Select-String -Path $f -Pattern 'deferring EndAudioSamples|catch-up timeout|EndAudioSamples returned|watchdog firing|ingest stall'

# Lip sync starvation (should produce NO hits during an utterance)
Select-String -Path $f -Pattern 'Low curve lead|Extrapolating'
```

What good looks like:

- No `Low curve lead` warnings during an utterance. One at SID=0 is the warmup and is expected.
- `Extrapolating` only at the very tail, in single-digit milliseconds.
- Watchdog `Unmatched` between 0.02 and 0.03.
- `EndAudioSamples returned wall=` around 165ms, reached via Wall catching Sent (or the 0.9s unmatched gate), not a 60s stall.
- No `Still deferring EndAudioSamples` lines repeating after Wall ≥ Sent.
- After a short reply, `ACE playback complete` then `listening window open (Speak)` — lantern green.
- No `ingest stall while HTTP still open` unless Brain/TTS actually hung (utt-8 class). After HTTP drain, stall must stay silent while EndAudioSamples is deferred.

Related diagnostics: `Godfrey.RecordAudio 1` / `Godfrey.RecordAudio 0` captures the master
submix to WAV (run the `0` while PIE is still active or nothing is written). Useful for
confirming whether a suspected glitch is real digital silence versus a visual stall.

---

## Open threads

- **~165ms flush is still a game-thread freeze.** Threshold tuning is exhausted. The
  remaining option is to move `EndAudioSamples` off the game thread: `FAudio2XSession`
  guards itself with a critical section and we only call it once every push is done, so it
  looks viable. Unverified: whether curve callbacks and `ActiveA2XSessions->Remove` are
  safe off the game thread.
- **Try chaining `D3D12Parameters` instead of `CudaParameters`** for CIG, since the CUDA
  variant crashes TensorRT.
- **`bLogPerAnimateChunkWallTime=True`** is diagnostic noise; turn it off once the flush
  question is closed.
- **Mic false-trigger:** STT transcribed unrelated room speech and Godfrey answered it.
  Needs gating before exhibition.
