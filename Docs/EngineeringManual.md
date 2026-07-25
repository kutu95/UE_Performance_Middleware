# Engineering Manual — Captain Godfrey (UE 5.8)

This is the living engineering guide for the **production master** project:

`D:\UE Projects\MetaHuman_Baseline_UE58_Test`

For architecture, plugins, validation, and limitations, start with:

**[UE58_Baseline.md](UE58_Baseline.md)**

---

## Principles

1. **Do not redesign working systems** (speech, ACE lip sync, MetaHuman, queue).
2. Prefer **additive** diagnostics, validation, and configuration.
3. Keep UE 5.6 originals untouched as backups.
4. Every utterance should be correlatable via **SpeechId** in logs.

---

## Day-to-day

| Task | How |
|------|-----|
| Open project | UE 5.8 → `UnrealPerformer.uproject` |
| Validate | Tools → Validate Godfrey Project |
| Perf HUD | PIE → F8 (if enabled in Project Settings) |
| Godfrey settings | Project Settings → Plugins → Unreal Performer (Godfrey / ACE) |
| Restore point | `git checkout restore/ue58-audio-ok-2026-07-10` |

---

## Related docs

| Doc | Purpose |
|-----|---------|
| `UE58_Baseline.md` | Architecture, pipeline, validation, roadmap |
| `UE58_COMPATIBILITY_NOTES.md` | 5.6 → 5.8 clone notes |
| `ACEPlugin.txt` | ACE / A2F plugin restore |
| `MetaHumanAssets.txt` | MetaHuman content not in git |
| `MigrationPlan.txt` | Historical phase plan (exhibit build-out) |
| `UE56_EditorScriptingGuidelines.md` | Editor Python / World Partition rules |

---

## Log channels (Phase 1)

Filter Output Log by:

`LogGodfreySpeech`, `LogGodfreyACE`, `LogGodfreyAudio`, `LogGodfreyBehaviour`, `LogGodfreyAnimation`, `LogGodfreyQueue`, `LogGodfreyPerfMonitor`, `LogGodfreyValidation`, `LogGodfreyPcmStream`, `LogACERuntime`
