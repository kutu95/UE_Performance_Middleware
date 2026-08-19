# Errol likeness rebuild (UE 5.8) — revised

**Goal:** Godfrey’s live face/body use the existing Errol MetaHuman identity, not the Victorian `MHC_CaptainGodfrey` stylization.

**Identity rebuild: skipped.** `/Game/MetaHumans/Errol/Identity/MH_ID_Errol` is already the solved identity. HQ photos are **QC / reference only** — they are not applied as photographic face textures. Skin remains MetaHuman baked lookdev from DNA.

| Source | Path | Role |
|--------|------|------|
| Identity (use as-is) | `/Game/MetaHumans/Errol/Identity/MH_ID_Errol` | Shape / DNA |
| Identity archive | `Content/MetaHumans/Errol/Identity_Archive_2026-07-26/` | Rollback |
| Live Link takes | `/Game/CaptureManager/Imports/Live_Link_Face/` (`MySlate_6`–`9`) | Already used for identity; not needed again unless re-solving |
| HQ photos | `Content/MetaHumans/Errol/Source/Photos/` | Side-by-side QC only |

---

## 1. Bind + Assemble `MHC_Errol` (editor — you) — NEXT

1. Open character definition **`MHC_Errol`**.
2. Confirm / bind identity **`MH_ID_Errol`** (do not re-run Mesh to MetaHuman).
3. Set hair / facial hair / brows to match Errol (drop captain handlebar / messy-beard defaults if they fight likeness).
4. **Assemble** as `MHC_Errol`.
5. Confirm `BP_MHC_Errol` uses:
   - `/Game/MetaHumans/MHC_Errol/Body/SKM_MHC_Errol_BodyMesh`
   - `/Game/MetaHumans/MHC_Errol/Face/SKM_MHC_Errol_FaceMesh`
   - Not `SKM_MHC_CaptainGodfrey_*`.
6. Spot-check vs photos. When happy, tell the assistant **ready**.

Leave `MHC_CaptainGodfrey` on disk as rollback.

---

## 2. Migrate performer (after Assemble)

Profile: `Config/PerformerCharacterProfile_Errol.json`  
Script: `Scripts/migrate_errol_to_godfrey_performer.py` (donor = `BP_MHC_Errol`; archives to `BP_Godfrey_Performer_CaptainGodfrey_Archive`).

```bat
"D:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" ^
  "D:\UE Projects\MetaHuman_Baseline_UE58_Test\UnrealPerformer.uproject" ^
  -ExecutePythonScript="D:/UE Projects/MetaHuman_Baseline_UE58_Test/Scripts/migrate_errol_to_godfrey_performer.py" ^
  -unattended -nop4 -nosplash -log
```

Then in editor: open `Godfrey_World` → Tools → Execute Python Script → `Scripts/swap_godfrey_world_to_mhc_performer.py`.

PIE: queue TTS → speech + ACE lip sync; confirm face is Errol.

---

## Status

| Step | Status |
|------|--------|
| Sources staged | Done |
| Identity archive copy | Done |
| Identity rebuild from Live Link + photos | **Skipped** (existing `MH_ID_Errol`) |
| Migrate tooling → Errol donor | Done |
| Assemble `MHC_Errol` on Errol meshes | **You — next** |
| Run migrate + level swap | After Assemble |
