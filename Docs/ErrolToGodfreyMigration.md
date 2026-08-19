# Errol → Godfrey performer migration (2026-07-26)

**Status (2026-07-26):** CaptainGodfrey MHC shell + ACE stack **PASS**.  
**Likeness follow-up (2026-08-02):** live appearance is still Captain stylization; Errol identity rebuild is in progress — see **[ErrolLikenessRebuild.md](ErrolLikenessRebuild.md)**. Migrate script/profile now target `MHC_Errol` / `SKM_MHC_Errol_*` after Assemble.

## What changed (2026-07-26)

| Item | Result |
|---|---|
| Bridge/Kristofer shell | Archived → `/Game/MetaHumans/Godfrey/BP_Godfrey_Performer_KristoferBridge_Archive` |
| New `BP_Godfrey_Performer` | Duplicate of MHC shell (then CaptainGodfrey meshes + stable clothing PP) |
| Face AnimClass | `Face_AnimBP` (Apply ACE) — was MHC `ABP_Face` |
| Body AnimClass | `GodfreyBodyAnimInstance` |
| Components added | ACEAudioCurveSource, AceWarmup, PerformanceState, AnimationBridge |
| Garment hacks | OFF (`bManageMetaHumanGarmentsAtRuntime=false`) |
| Body montages | **ON** (`bEnableBodyMontages=true`) — AS_* performance library → DefaultSlot via AnimationBridge |
| GM queue poll | Already present on `GM_Godfrey_Exhibit` |

Profile: `Config/PerformerCharacterProfile_Errol.json` (now Errol likeness donor paths).

### Performance library (body gestures)

| Item | Path / note |
|---|---|
| AnimSequences | `/Game/Godfrey/Animation/Animation/Performances/AS_*` |
| Montages (optional authored) | `AM_<same stem>` DefaultSlot — see `Scripts/setup_godfrey_performance_library_montages.py` |
| Cue contract | [`Docs/GodfreyPerformanceCueContract.md`](GodfreyPerformanceCueContract.md) |
| Middleware catalog | `Config/GodfreyPerformanceActionCatalog.json` |
| Authoring-only (do not require for cook) | `Perf_*`, Capture Manager imports, companion `SK_*` export meshes |

## Your next steps (editor)

Likeness: follow [`Docs/ErrolLikenessRebuild.md`](ErrolLikenessRebuild.md) (Identity → Assemble MHC_Errol → migrate).

After migrate PASS:

1. Open `Godfrey_World`, load the exhibit region.
2. Tools → Execute Python Script → `Scripts/swap_godfrey_world_to_mhc_performer.py`
3. PIE: queue TTS → speech + lip sync; confirm Errol face.

## Scripts

- `Scripts/migrate_errol_to_godfrey_performer.py` — Errol donor → `BP_Godfrey_Performer` + ACE wiring
- `Scripts/swap_godfrey_world_to_mhc_performer.py` — editor-only level tag/sanity
- `Saved/run_migrate.bat` — forward-slash launcher (paths with `\UE` break Python load)