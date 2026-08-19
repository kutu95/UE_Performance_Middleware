# UE 5.8 Baseline — Captain Godfrey

**Project:** `D:\UE Projects\MetaHuman_Baseline_UE58_Test`  
**Engine:** Unreal Engine 5.8  
**Status:** Production master for all future Godfrey development  
**Restore tag:** `restore/ue58-audio-ok-2026-07-10` (pre–Phase 1 hardening)  
**Phase 1:** Project hardening (diagnostics / validation / docs) — **no behaviour redesign**

---

## 1. Current architecture

```
Web path (unchanged):
  Browser mic → Web Speech → POST /api/chat → exhibition TTS queue
        │
        ▼
UGodfreyExhibitionQueuePollComponent  (GET /api/exhibition/unreal-tts-status)
        │  ready → PullQueuedGodfreySpeechToAudio (ttsOnly)

Game mic path (additive):
  UE always-on mic → ws://Brain/api/unreal/stt → OpenAI Realtime STT (server_vad)
        │  transcript_completed
        ▼
UGodfreyDirectSpeechComponent::AskGodfrey → POST /api/godfrey/speak/stream-pcm (text)

Shared reply playback:
        ▼
UGodfreyPcmStreamSession
        ├── AnimateFromAudioSamples → LocalA2F-Mark (NvAudio2FaceMark)
        ├── ACE curves → Face AnimBP (lip sync)
        └── Parallel PCM audible (optional) → speakers
                │
                ▼
UGodfreyPerformanceStateComponent (BeginSpeaking / EndSpeaking)
                │
                ▼
UGodfreyPerformerAnimationBridgeComponent (body montages — ON by default; AS_* library under Performances)
```

**Character:** `BP_Godfrey_Performer` (Captain Godfrey / MHC) in `Godfrey_World`.  
**Game mode:** `GM_Godfrey_Exhibit` (queue poll + optional VoiceInput/DirectSpeech).

### Body performance library

- Sequences: `/Game/Godfrey/Animation/Animation/Performances/AS_*`
- Cue contract: `Docs/GodfreyPerformanceCueContract.md`
- Catalog for middleware: `Config/GodfreyPerformanceActionCatalog.json`
- Cook/runtime: `AS_*` / `AM_*` + body mesh/ABP. Authoring-only: `Perf_*`, Capture Manager media, `SK_*` companions.

### Runtime modules

| Module | Role |
|--------|------|
| `UnrealPerformer` | Speech, PCM→ACE, queue, performance state, body anim, diagnostics, HUD |
| `UnrealPerformerEditor` | Tools → Validate Godfrey Project |

### Diagnostics (Phase 1)

| Channel | Log category |
|---------|----------------|
| Speech | `LogGodfreySpeech` |
| ACE | `LogGodfreyACE` (plus existing `LogACERuntime` / `LogGodfreyPcmStream`) |
| Animation | `LogGodfreyAnimation` |
| Behaviour | `LogGodfreyBehaviour` |
| Audio | `LogGodfreyAudio` |
| Queue | `LogGodfreyQueue` |
| Vision | `LogGodfreyVision` (reserved) |
| Performance | `LogGodfreyPerfMonitor` |

Each utterance gets a **SpeechId** (`utt-<ordinal>-<requestIdPrefix>`) that follows the pipeline stages.

Timing summary (ms from SpeechId T0):

`[Performance] TimingMs SpeechId=… | AudioRecv=… AceBegin=… PlayBegin=… SpeechDone=…`

Runtime HUD (PIE, toggle **F8**): FPS, frame ms, SpeechId, latency, queue length, behaviour state, animation name, emotion (reserved).

---

## 2. Current plugins (required)

| Plugin | Purpose |
|--------|---------|
| `NV_ACE_Reference` | ACE runtime / AudioCurveSource |
| `NvAudio2FaceMark` | LocalA2F-Mark provider |
| `MetaHuman` / `MetaHumanSDK` / `MetaHumanCharacter` / `MetaHumanCoreTech` / `MetaHumanLiveLink` | MetaHuman character |

Large third-party ACE binaries are **not** in git (see `Docs/ACEPlugin.txt`).

---

## 3. Working pipeline (do not replace)

1. Queue poll (~1s) → status ready + `requestId`
2. PCM stream at 24 kHz mono into ACE LocalA2F-Mark
3. Face lip sync from ACE blend shapes
4. Audible speech via parallel PCM path (ACE curves remain for sync)
5. Performance state auto-speaking from utterance start/end
6. Body montages **disabled** (`bEnableBodyMontages=false`) until a custom gesture library exists — face/ACE unaffected

Project settings: **Plugins → Unreal Performer (Godfrey / ACE)**  
Config: `Config/DefaultEngine.ini` → `[/Script/UnrealPerformer.UnrealPerformerGodfreySettings]`

---

## 4. Validation procedure

### In Editor

1. Open `Godfrey_World`
2. **Tools → Validate Godfrey Project**
3. Read Output Log (`LogGodfreyValidation`)
4. Expect overall **PASS** or **WARNING** (body montages OFF is a documented WARNING)

### Manual smoke (behaviour must match today)

1. Start Godfrey Brain on `http://localhost:3000`
2. PIE `Godfrey_World`
3. Confirm startup log: `bAllowBackgroundAudio forced true` / UnfocusedVolume
4. Trigger exhibition speech
5. Confirm full audible utterance + face lip sync
6. Confirm structured logs: `[Speech] Speech Generated` → `Audio Ready` → `ACE Started` → `Audio Playback Started` → `Behaviour Started` → `Speech Finished`

### Console helpers

- `ace.GodfreyStartupTiming 1` — ACE internal Play vs curve timing
- Project setting `bGodfreyLogAudiblePlaybackDiagnostics` — mixer / AppMult diagnostics

---

## 5. Known limitations

| Item | Notes |
|------|-------|
| Body montages off | Intentional; Mixamo talking looked rubbery on MetaHuman |
| Hold-play until stream end | `bGodfreyAceHoldPlayUntilStreamEnd=True` in ini — better lip sync, slower time-to-first-word |
| Sample-rate defaults | Exhibition paths use 24000; some API defaults historically 16000 — settings now expose `GodfreyDefaultStreamSampleRate` |
| Parallel + ACE audio | Parallel audible ON; ACE mute-on-parallel OFF — possible double path if both audible |
| ACE third-party not in git | Must restore from plugin package / junction |
| MetaHuman content not in git | See `Docs/MetaHumanAssets.txt` |
| Vision / Emotion | Reserved diagnostics only — not implemented |

---

## 6. Future roadmap (do not implement in Phase 1)

| Phase | Focus |
|-------|--------|
| 2 | Body gesture library + re-enable montages safely |
| Later | Vision, Emotion, mocap, environment, costume, historical personality |

---

## 7. Engineering notes

- **Do not redesign** speech, ACE, MetaHuman, or queue systems.
- Prefer additive diagnostics and config over behavioural changes.
- UE 5.6 originals (`MetaHuman_Baseline_Test`, `Test_Live_Audio`) remain known-good backups; this UE 5.8 project is the master.
