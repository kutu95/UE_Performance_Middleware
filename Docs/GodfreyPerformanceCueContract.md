# Godfrey performance cue contract (middleware ↔ Unreal)

Brain / exhibition middleware drives Godfrey through `UGodfreyPerformanceStateComponent::NotifyPerformanceCue` (also forwarded from `UAsyncActionStreamGodfreySpeech`).

Live mic (`POST /api/godfrey/speak/stream-pcm`): markers are stripped before ElevenLabs. Parsed `performanceEvents` are stored against `speakRequestId` and Unreal polls `GET /api/godfrey/speak/performance-events?requestId=` while PCM streams.

Queued TTS: the same events arrive on `GET /api/exhibition/unreal-tts-status`.

Body gestures play on `UGodfreyPerformerAnimationBridgeComponent` (`UpperBody` overlay over a planted `DefaultSlot` stance). Face / ACE lip sync is unchanged and stays on the Face mesh.

## Cue shapes

| Intent | `type` | `value` | Result |
|--------|--------|---------|--------|
| Coarse state | `state` (alias: `performer`) | `idle` \| `listening` \| `thinking` \| `speaking` \| `emphasis` \| `amused` \| `serious` | PerformanceState helpers → bridge behaviour montages. While an utterance is in progress, `emphasis` / `amused` / `serious` are **mood only** (stay Speaking; do not start post-speech Listening*). `listening` / `thinking` / `idle` are ignored until ACE audio ends. |
| Named action | `action` (also `performance` / `gesture`) | `TwoThumbsUp_01` or `AS_TwoThumbsUp_01` | Bridge resolves `AM_`/`AS_` under `/Game/Godfrey/Animation/Animation/Performances` |
| Legacy short tokens | `listen` / `think` / `speak` / … | optional | Still routed when short (≤12 chars); named `_01` ids are **not** coarse-matched |

Unknown cues are ignored safely (logged; no crash). Named ids that are missing from the library log a warning and leave the current coarse state alone.

## Godfrey Brain markers (`D:\Godfrey`)

Brain composes reply text with inline markers, then `lib/performance-text.js` parses them into `performanceEvents`. Markers are stripped before ElevenLabs. Live mic delivers events via `GET /api/godfrey/speak/performance-events`; the queued path uses `GET /api/exhibition/unreal-tts-status`.

| Marker in reply | Emitted event |
|-----------------|---------------|
| `[thinking]` / `[serious]` / … | `{ type: "state", value: "thinking" }` |
| `[farewell]` / `[goodbye]` | `{ type: "state", value: "farewell" }` → Unreal farewell → sea idle |
| `[gesture:TwoThumbsUp_01]` | `{ type: "action", value: "TwoThumbsUp_01" }` |
| `[action:…]` or `[TwoThumbsUp_01]` | same `action` when id is in catalog |
| `*looks down*` | `{ type: "gaze", … }` (optional; Unreal may ignore) |

Catalog for the LLM (with descriptions):

- Brain: `D:\Godfrey\config\godfrey-performance-action-catalog.json`
- Loaded via `lib/gesture-catalog.js` → `GESTURE_CATALOG_ADDENDUM` appended to Claude + OpenAI system prompts
- UE mirror: `Config/GodfreyPerformanceActionCatalog.json`

Restart the Brain Node process after catalog / prompt changes.

## Exhibition presence (always animated)

| Presence | Behaviour |
|----------|-----------|
| SeaIdle | Loop `AS_IdleLookingToSea_01` at BeginPlay (not frozen ref pose) |
| Engaging | First visitor speech: skip Welcome (R2); optional turn skip → Listening pool (R3) |
| Conversing | Listening / Thinking / Speaking as usual; after speak returns to Listening |
| Farewell | `[farewell]` or 60s quiet (`VisitorIdleTimeoutSeconds`) → `FarewellWave` → turn to sea → SeaIdle |

### Early “reply incoming” (LLM wait)

While the visitor’s question is accepted but the assistant reply is not yet queued, Brain status returns:

```json
{ "ready": false, "phase": "awaiting_reply", "requestId": "..." }
```

UE’s exhibition poller calls `UGodfreyPerformanceStateComponent::NotifyReplyIncoming(requestId)` (deduped per id). The animation bridge keeps Conversing camera lock but plays `ListeningEnterMontage` (`AwaitBrainListening`) instead of the visitor-await `IdleBreathing` hold. Cleared when status is ready, pending expires/errors (`ClearReplyIncoming`), or speaking starts.

Upper-body montage blend: spine_01 branch (full arm/hand chain), weight **1.0** (`GodfreyUpperBodyMontageBlendWeight`) so planted IdleStanding cannot leak into head/gaze. In dialog, body `neck_01` LookAt aims at `Exhibit_CineCamera` (clamped). Legs stay on planted `IdleStanding_01` unless the action is listed in `GodfreyApplyRootMotionActions`. Do not re-enable Face post-process gaze.

## Speaking policy

- While speaking, Brain `[gesture:…]` owns UpperBody. Brain must plan **distinct** takes to cover the whole spoken line (never the same CatalogId twice). Unreal plays each once. If Brain omits a cue or the take ends while he is still talking, Unreal fills with a different basic explaining clip — not idle, not a repeat. Cues during Engaging Welcome prefetch are stashed until audible speech.
- Named speaking/gesture clips are one-shots: story `[gesture:]` takes own UpperBody until they **end**. Unreal must not play a later Brain cue over a live take — queue it. Cues often arrive **before** ACE `OnAnimationStarted` (LLM tokens, not spoken beats); speak-start must not start the speaking pool over that take. Welcome / Greeting overlays are interrupted when audible speech starts (short blend) so baked visemes cannot fight ACE. After a story take ends, the next queued Brain `[gesture:]` or a different pool clip may resume. Do not start two pool clips from the same ending (tick maintain + montage-ended).
- Pause / gaze / expression / `[amused]` markers are not body takes and must not restart or replace a Brain speaking take.
- Live mic: Unreal polls `GET /api/godfrey/speak/performance-events` during `stream-pcm` and plays `[gesture:…]` as named overlays.
- ACE / PCM utterance start/end still drive `BeginSpeaking` / `EndSpeaking` via `bAutoSpeakingStateFromUtterance`.
- Dialog overlay blend ~1.5s / 1.6s. Long replies need several `[gesture:]` markers (one per ~10–18s beat), not a single cue.

## Action catalog

Machine-readable list for middleware:

- `Config/GodfreyPerformanceActionCatalog.json` — valid action stems + default slot map + cook vs authoring notes.
- Brain enriched copy with descriptions — see above.

Optional Unreal DataTable: assign `PerformanceActionTable` on the bridge (`FGodfreyPerformanceActionRow`) to override path-based lookup.

## Enable flag

`bEnableBodyMontages` defaults **true** once the performance library is wired. Set false on the bridge to park body plays without touching ACE. Confirm PIE log shows `bEnableBodyMontages=1`.

## Editor setup

```
UnrealEditor-Cmd.exe ".../UnrealPerformer.uproject"
  -ExecutePythonScript=".../Scripts/setup_godfrey_performance_library_montages.py"
  -unattended -nop4 -nosplash -log
```

Creates `AM_*` next to `AS_*` when possible and assigns bridge slots. If AM assets are missing, BeginPlay builds dynamic DefaultSlot montages from the soft-default `AS_*` sequences.

## PIE validation checklist

1. Confirm log: `performance library defaults assigned` and `bEnableBodyMontages=1`.
2. No chat yet: Godfrey loops look-to-sea (not frozen); in dialog the head aims at the cine camera (R21).
3. First Unreal-targeted question: turn → welcome → listening → thinking → speaking.
4. After speak: returns to listening presence, still alive.
5. Wait ~60s or Brain `[farewell]`: farewell wave → look to sea again.
6. Call `NotifyNamedPerformanceAction("TwoThumbsUp_01")` (or cue type=`action`, value=`TwoThumbsUp_01`).
7. From Brain (live mic or Unreal output): reply containing `[gesture:DescribingWhere_01]` → Unreal log `Godfrey performance cue` / `cue type="action"` → named overlay plays; ElevenLabs text has no marker.
8. Unknown action id only warns; coarse state unchanged. Lip sync / ACE unchanged.
