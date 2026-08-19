# Victorian costume on BP_Godfrey_Performer (2026-07-26)

**Status:** Wiring **PASS** (`Saved/DressVictorianCostumeOnGodfrey.txt`)

## What was done

1. Built `UGodfreyCostumeRetargetAnimInstance` (IK Retarget Pose From Mesh, Body → costume).
2. Created `/Game/MetaHumans/Costume/Retargeting/IK_Victorian_Genesis8` (auto Genesis retarget definition).
3. Created `/Game/MetaHumans/Costume/Retargeting/RTG_MetaHuman_To_Victorian` (MH IK → Victorian IK, fuzzy chain map).
4. On `BP_Godfrey_Performer`:
   - Added `VictorianCostume` skeletal mesh (`Victorian_Gentleman`)
   - AnimClass = `GodfreyCostumeRetargetAnimInstance`
   - Hid MHC default outfit component `SkeletalMesh`
   - Body + costume set to **Always Tick Pose and Refresh Bones**

Face / ACE / speech stack unchanged.

## PIE checklist

1. Open project, open `Godfrey_World`, PIE.
2. Confirm Victorian suit follows body motion; log: `GodfreyCostumeRetarget: ready ...`
3. If shoulders/hips look wrong: open `RTG_MetaHuman_To_Victorian`, fix chain mapping / retarget pose, save.
4. If body skin shows through clothes: hide Body mesh (keep Face) or enable body invisibility under garments.
5. If default MHC shorts/shirt still visible: hide remaining `SkeletalMesh1` / `SkeletalMesh2` on the BP.

## Re-run

```
Saved/run_dress_victorian.bat
```
or Tools → Execute Python Script → `Scripts/dress_victorian_costume_on_godfrey.py`
