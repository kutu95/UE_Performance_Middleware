# Phase 1 Validation Report — Captain Godfrey (UE 5.8)

**Project:** `D:\UE Projects\MetaHuman_Baseline_UE58_Test`  
**Date:** 2026-07-10  
**Scope:** Consistency audit + Phase 1 hardening (no behaviour redesign)

---

## 1. Baseline consistency (Version 1.0)

| System | Status | Notes |
|--------|--------|-------|
| Speech pipeline | **PASS** | Queue → `stream-pcm` → `UGodfreyPcmStreamSession` |
| ACE facial animation | **PASS** | LocalA2F-Mark / `UACEAudioCurveSourceComponent` |
| Audio playback | **PASS** | Parallel PCM audible; PIE mute fix retained |
| MetaHuman | **PASS** | Kristofer / `BP_Godfrey_Performer` |
| Animation Blueprint | **PASS** | Body AnimClass + face ACE curves |
| Body animation | **WARNING** | `bEnableBodyMontages=false` (intentional park) |
| Queue | **PASS** | `UGodfreyExhibitionQueuePollComponent` on `GM_Godfrey_Exhibit` (GameMode), not a placed level actor |
| Required plugins | **PASS** | NV_ACE_Reference, NvAudio2FaceMark, MetaHuman* |

### Documented inconsistencies (not fixed — preserve behaviour)

1. **`bGodfreyAceHoldPlayUntilStreamEnd`:** C++ default `false`, `DefaultEngine.ini` = `True` → shipped behaviour is hold-play ON.
2. **Body montages OFF** while older docs may imply they work — Phase 1 parks them deliberately.
3. **Sample rate:** exhibition defaults 24000 Hz; some older API entry points used 16000 — settings now expose `GodfreyDefaultStreamSampleRate=24000`.
4. **Parallel audible + ACE mute off:** both paths can be audible if ACE volume stays non-zero (`bGodfreyMuteAceWhenParallelAudibleStarts=False`).

---

## 2. Editor validation tool

**Tools → Validate Godfrey Project** (`LogGodfreyValidation`)

Run after opening `Godfrey_World`. Expected overall: **PASS** or **WARNING** (body montages OFF is a WARNING).

Checks: plugins, performer presence/duplicates, ACE, AnimBP, Leader Pose, queue component, required assets.

---

## 3. Compile status

**PASS** — `UnrealPerformerEditor` Win64 Development built successfully against UE 5.8 (2026-07-10).

---

## 4. Manual smoke (behaviour unchanged)

1. Brain on `http://localhost:3000`
2. PIE `Godfrey_World`
3. Confirm audible speech + lip sync
4. Confirm SpeechId stage logs and optional F8 HUD
