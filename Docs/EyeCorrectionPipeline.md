# Eye Correction Pipeline (Phase 9)

## Important clarification

**MetaHuman Animator cannot force eyes to match Errol.**

The Process / Details panel only re-solves from monocular video. If the solve produces downcast eyes, more Process tweaks usually will not fix it.

The real “eye correction pass” happens **after Export Animation**, in Sequencer / Animation Mode, using the Face Control Rig.

## Goal

Keep:

- head attached
- ACE lipsync working

Fix:

- baked downcast eye gaze in solved `AS_*` clips

by producing corrected assets:

- `AS_<CueId>_EyeFixed`
- optional `AM_<CueId>_EyeFixed`

## Step-by-step for Listening_01

### A. Export the solved performance (do not try to force eyes here)

1. Keep `Perf_Listening_01` open in MetaHuman Animator.
2. Accept that lipsync/head are good enough.
3. Click **Export Animation**.
4. Export into:
   - `/Game/Godfrey/Animation/Animation/Performances/`
5. Name it:
   - `AS_Listening_01` (or keep existing if already exported)
6. Do **not** expect eyes to be correct yet.

### B. Open the exported animation for eye fix

1. In Content Browser, open `AS_Listening_01`.
2. Or create a temporary Level Sequence and add:
   - `BP_Godfrey_Performer`
   - the face animation track from `AS_Listening_01`
3. Enter **Animation Mode** (toolbar).
4. Select Godfrey’s **Face** mesh / Face Control Rig (`Face_ControlBoard_CtrlRig`).

### C. Force eyes forward with Face Control Rig

Find these controls (names may vary slightly by MetaHuman version):

- `CTRL_C_eyesAim` (preferred aim target)
- and/or expression curves:
  - `CTRL_expressions_eyeLookUpL/R`
  - `CTRL_expressions_eyeLookDownL/R`
  - `CTRL_expressions_eyeLookLeftL/R`
  - `CTRL_expressions_eyeLookRightL/R`

Do this:

1. Scrub to a frame where Errol looks straight ahead.
2. Move `CTRL_C_eyesAim` so Godfrey looks straight at camera / visitor.
3. If using look curves instead:
   - reduce `eyeLookDown*` toward `0`
   - increase `eyeLookUp*` slightly until gaze matches Errol
4. Set keys on those eye controls across the clip (or key at start/end if gaze should stay mostly forward).
5. Scrub and compare against Errol until acceptable.

### D. Bake corrected result

1. Bake Control Rig / animation changes to a new sequence.
2. Save as:
   - `/Game/Godfrey/Animation/Animation/Performances/AS_Listening_01_EyeFixed`
3. If you need a montage for runtime:
   - create `AM_Listening_01_EyeFixed` on `DefaultSlot`

### E. Mark plan item done

In `Config/GodfreyEyeCorrectionPlan.json`, update the `Listening_01` item:

- `"status": "done"`
- `"notes": "Forced forward gaze via Face Control Rig / eyesAim"`

## What NOT to do

- Do not keep fighting Process settings hoping eyes will suddenly match.
- Do not re-enable runtime Face post-process LookAt (it broke head attach + lipsync).
- Do not overwrite the original `AS_Listening_01` until you are happy with the EyeFixed version.

## Recommended: programmatic curve offset (UE 5.8)

Manual Sequencer / Control Rig eye editing is fragile in 5.8 (AnimBP + Control Rig overrides). Prefer the Python offset script.

### Script

`Scripts/offset_eye_gaze_curves.py`

It copies `AS_<Stem>` → `AS_<Stem>_EyeFixed`, then applies:

**Eyes (same on L and R):**
- `eyeLookDownL/R` −= `GAZE_LIFT`
- `eyeLookUpL/R` += `GAZE_LIFT`
- Both sides are offset together by `offset_eye_pair`. If a capture left one eye's curve unkeyed, that
  side is seeded from the other eye's key times — otherwise only one eyeball moves and Godfrey goes
  wall-eyed (this is what happened to `AS_FarewellFinishSpeaking_01`: `eyeLookUpR` +0.62, `eyeLookUpL` 0).

**Left/right symmetry:**
- `BALANCE_CURVE_BASES` (default `eyeSquintInner`) is averaged between the two eyes per key. The MetaHuman
  Animator solves squint Godfrey's right eye about twice as hard as his left (library mean 0.144 vs 0.082,
  right higher in 63 of 65 clips), which reads to viewers as one odd eye.
- `MIRROR_CURVE_BASES` (default `eyeWiden` + `eyeLookLeft/Right/Up/Down`) copies the **right** eye's
  curve onto the left, rather than averaging — the right eye is the reference side.
  - **Widen:** the solve opens Godfrey's left lid wider (library mean 0.164 vs 0.155), so that eye shows
    more sclera and viewers report it as "lighter in colour."
  - **Gaze:** `eyeLookLeft` / `eyeLookRight` diverge (right higher on look-left in 56 of 66 clips before
    the mirror). After the pass both eyes share the right eye's aim; right is unchanged.
- `FORCE_PARALLEL_LOOK_DIRECTION` keys `eyeParallelLookDirection` to 1 so both eyeballs share a look
  direction. The captures leave that control unkeyed, so each eye otherwise follows its own `eyeLook*`
  curves and any residual difference shows as divergent gaze.

Still unbalanced after the current pass (lid timing, not aim): `eyeBlink` (mean L 0.212 vs R 0.203,
worst `IdleLookingToSea_02`). Add it to `MIRROR_CURVE_BASES` if blink asymmetry still reads as odd.

Audit any time with `Scripts/audit_eye_curve_symmetry.py` (writes `Saved/EyeCurveSymmetryAudit.txt`/`.json`),
then summarise with `python Scripts/analyze_eye_symmetry_report.py`.

**Frown / concerned brow (same on L and R):**
- `browDown*`, `mouthCornerDepress*` × `FROWN_SCALE`
- optional: `browLateral*`, `noseWrinkle*` (same scale)

### Trial-and-error

1. Open `Scripts/offset_eye_gaze_curves.py`
2. Set:
   - `SOURCE_STEM = "ListeningConcerned_01"`
   - `GAZE_LIFT = 0.25` (try `0.15` … `0.40`)
   - `FROWN_SCALE = 0.25` (try `0.0` … `0.5`; `1.0` = no frown change)
   - `ALWAYS_COPY_FROM_SOURCE = True` (rebuilds from pristine each run — offsets do not stack)
3. In Unreal: **Tools → Execute Python Script** → pick that file
4. Preview `AS_*_EyeFixed` on Godfrey in Sequencer (**mute Face Control Rig**)
5. Change `GAZE_LIFT` / `FROWN_SCALE` and re-run until gaze and expression look right

Optional fine knobs: `DOWN_EXTRA`, `UP_EXTRA`, `LEFT_EXTRA`, `RIGHT_EXTRA`, `FROWN_RELAX`.

### Batch all clips

1. In `Scripts/offset_eye_gaze_curves.py` set `BATCH_ALL_IN_LIBRARY = True` (already set for full library pass).
2. Re-run the script — it writes `AS_*_EyeFixed` for every source `AS_*` and never modifies originals.
3. Runtime: `bPreferEyeFixedLibraryVariants` on `GodfreyPerformerAnimationBridgeComponent` (default **true**) remaps playback to `*_EyeFixed` when present.

Report written to: `Saved/EyeGazeCurveOffset.txt`

Curve edits are wrapped in a data-model bracket (`CurveEditBracket`). Without it the controller
recompresses the whole sequence after every key — about 25s per curve, hours for the library.

Headless is the fastest way to run the batch, but Unreal's command line splits on the space in
`UE Projects`, so copy the script somewhere without spaces first:

```powershell
Copy-Item "D:\UE Projects\MetaHuman_Baseline_UE58_Test\Scripts\offset_eye_gaze_curves.py" "D:\eye_offset_tmp.py"
& "D:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" `
  "D:\UE Projects\MetaHuman_Baseline_UE58_Test\UnrealPerformer.uproject" `
  -ExecutePythonScript="D:/eye_offset_tmp.py" -unattended -nop4 -nosplash -NullRHI
```

## Eye colour is not a variable — do not chase it

If someone reports one eye being a different colour, it is not the eye assets. Verified on
`MHC_CaptainGodfrey`:

- `T_EyeIrisL_BC` and `T_EyeIrisR_BC` export to byte-identical PNGs (mean RGB 53.500 / 57.813 / 49.122).
  The sclera pair differs by 0.03% luma, which is vein noise.
- All 115 parameters on `MI_EyeL_Baked` and `MI_EyeR_Baked` resolve to the same effective values, and a
  full editor-property diff of the two instances is empty. The instances have different parents
  (left → `MI_eye_eyeball_unified_MH_preset_left`, right → `Common/.../Baked/MI_EyeL_Baked`) but the
  chains resolve identically. Errol is baked the same way, so this is MetaHuman output, not a project bug.
- The left instance used to override `Cloudy Eye Intensity/Size/Softness/Variation` and
  `Melanosis Ring Opacity Variation Rotation`. Those are gated behind `Use Cloudy Eye` and
  `Use Limbal Melanosis Ring`, both **off**, so they never rendered. They were dropped so the left
  inherits the right eye's values — see `Scripts/fix_godfrey_eye_material_symmetry.py`.

Re-check any time with `Scripts/audit_godfrey_eye_material_symmetry.py`,
`Scripts/audit_godfrey_eye_textures.py` and `Scripts/diff_godfrey_eye_instances.py`.

An apparent left/right eye difference is lid aperture (`eyeWiden`, `eyeBlink`), gaze (`eyeLook*`), or
scene lighting — chase those instead.

## Runtime later

`bPreferEyeFixedLibraryVariants` (default true) makes the app prefer `AS_*_EyeFixed` / `AM_*_EyeFixed` at play time. Original library assets stay intact. Turn the flag off on the bridge component to revert to originals without deleting EyeFixed assets.
