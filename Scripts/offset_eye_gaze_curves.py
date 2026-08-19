"""Offset MetaHuman face curves on performance AnimSequences (UE 5.8).

Trial-and-error correction for:
  - eye gaze (eyeLook* lift, applied identically to both eyes)
  - left/right eye symmetry (capture solves squint one eye harder than the other, and opens the
    left lid wider, which reads as one lighter eye)
  - heavy frown / concerned brow (scale down browDown, mouthCornerDepress, etc.)
  - baked speech / viseme mouth motion (zero jaw/tongue/mouthClose/… — ACE owns live lipsync)

Workflow:
  1. Edit the CONFIG block below.
  2. In Unreal: Tools -> Execute Python Script -> this file.
  3. Preview AS_*_EyeFixed on Godfrey (Face Control Rig muted).
  4. Tweak GAZE_LIFT / FROWN_SCALE and re-run.
     ALWAYS_COPY_FROM_SOURCE=True rebuilds from pristine each time (no stacking).

Headless:
  UnrealEditor-Cmd.exe "D:/UE Projects/MetaHuman_Baseline_UE58_Test/UnrealPerformer.uproject"
    -ExecutePythonScript="D:/UE Projects/MetaHuman_Baseline_UE58_Test/Scripts/offset_eye_gaze_curves.py"
    -unattended -nop4 -nosplash -log
"""
from __future__ import annotations

import bisect
import json
from pathlib import Path

import unreal

# ---------------------------------------------------------------------------
# CONFIG — edit these between trial runs
# ---------------------------------------------------------------------------

LIBRARY = "/Game/Godfrey/Animation/Animation/Performances"

# Single-clip trial when BATCH_ALL_IN_LIBRARY and BATCH_FROM_PLAN are False.
SOURCE_STEM = "Concerned_01"  # without AS_ prefix
OUTPUT_SUFFIX = "_EyeFixed"

# Rebuild output from pristine source every run (recommended for tuning).
ALWAYS_COPY_FROM_SOURCE = True

# --- Eyes ---
# Primary knob: reduce Down + raise Up equally on L and R.
# Typical trial range for downcast MetaHuman Animator solves: 0.10 .. 0.45
GAZE_LIFT = 0.48

# Optional extras (applied after GAZE_LIFT, same on L and R).
DOWN_EXTRA = 0.08  # additional subtract from eyeLookDown*
UP_EXTRA = 0.14  # additional add to eyeLookUp*
LEFT_EXTRA = 0.0  # add to eyeLookLeft*
RIGHT_EXTRA = 0.0  # add to eyeLookRight*

# --- Left/right eye symmetry ---
# MetaHuman Animator solved Godfrey's right eye far more squinted than his left (mean 0.144 vs
# 0.082 across the library), which reads as one odd eye. Average the two sides per key.
BALANCE_EYE_CURVES = True
BALANCE_CURVE_BASES = [
    "CTRL_expressions_eyeSquintInner",
]

# Curves where the right eye is the reference and the left is made to match it, rather than the
# two being averaged. eyeWiden: left lid opened wider (read as lighter eye). eyeLook*: divergent
# gaze — right is correct; copy R→L so both eyeballs share the same aim.
MIRROR_RIGHT_ONTO_LEFT = True
MIRROR_CURVE_BASES = [
    "CTRL_expressions_eyeWiden",
    "CTRL_expressions_eyeLookLeft",
    "CTRL_expressions_eyeLookRight",
    "CTRL_expressions_eyeLookUp",
    "CTRL_expressions_eyeLookDown",
]

# Nothing in the captured clips keys eyeParallelLookDirection, so each eyeball follows its own
# eyeLook* curves and any residual L/R difference reads as a divergent gaze.
FORCE_PARALLEL_LOOK_DIRECTION = True
PARALLEL_LOOK_DIRECTION_CURVE = "CTRL_expressions_eyeParallelLookDirection"
PARALLEL_LOOK_DIRECTION_VALUE = 1.0

# --- Frown / concerned brow ---
# Multiply frown-related curves by this (same on L and R).
#   1.0 = leave frown as captured
#   0.25 = keep 25% of frown (strong relax)
#   0.0 = fully flatten frown shapes
FROWN_SCALE = 0.0

# Extra subtract after scale (usually leave 0; use if scale alone is not enough).
FROWN_RELAX = 0.10

# Soften lateral / wrinkle contribution to a “concerned” face (uses FROWN_SCALE too).
RELAX_BROW_LATERAL = True
RELAX_NOSE_WRINKLE = True

# --- Soft smile (less sad) ---
# Add to mouthCornerPull L/R (MetaHuman smile). Typical small smile: 0.08 .. 0.25
SMILE_AMOUNT = 0.15
# Optional tiny cheek raise with the smile (same on L and R). 0 = off.
CHEEK_RAISE = 0.06

CLAMP_MIN = 0.0
CLAMP_MAX = 1.0

# If True, only log what would change; do not write assets.
DRY_RUN = False

# Apply the same knobs to every AS_* in the Performances library (skips existing *_EyeFixed sources).
# Original AS_* assets are never modified — only AS_*_EyeFixed copies are written.
BATCH_ALL_IN_LIBRARY = True

# Alternate: only stems listed in Config/GodfreyEyeCorrectionPlan.json
BATCH_FROM_PLAN = False
PLAN_PATH = "Config/GodfreyEyeCorrectionPlan.json"

REPORT = "EyeGazeCurveOffset.txt"

# ---------------------------------------------------------------------------

# Keyed by curve base — the L and R suffixes are added by the pair pass so the two eyes can
# never receive different corrections.
EYE_PAIR_DELTAS = {
    "CTRL_expressions_eyeLookDown": lambda: -(GAZE_LIFT + DOWN_EXTRA),
    "CTRL_expressions_eyeLookUp": lambda: (GAZE_LIFT + UP_EXTRA),
    "CTRL_expressions_eyeLookLeft": lambda: LEFT_EXTRA,
    "CTRL_expressions_eyeLookRight": lambda: RIGHT_EXTRA,
}

# Core frown shapes (always scaled when FROWN_SCALE != 1).
FROWN_CORE_CURVES = [
    "CTRL_expressions_browDownL",
    "CTRL_expressions_browDownR",
    "CTRL_expressions_mouthCornerDepressL",
    "CTRL_expressions_mouthCornerDepressR",
]

FROWN_LATERAL_CURVES = [
    "CTRL_expressions_browLateralL",
    "CTRL_expressions_browLateralR",
]

FROWN_WRINKLE_CURVES = [
    "CTRL_expressions_noseWrinkleL",
    "CTRL_expressions_noseWrinkleR",
    "CTRL_expressions_noseNasolabialDeepenL",
    "CTRL_expressions_noseNasolabialDeepenR",
]

SMILE_CURVE_DELTAS = {
    "CTRL_expressions_mouthCornerPullL": lambda: SMILE_AMOUNT,
    "CTRL_expressions_mouthCornerPullR": lambda: SMILE_AMOUNT,
    "CTRL_expressions_mouthUpperLipRaiseL": lambda: CHEEK_RAISE * 0.5,
    "CTRL_expressions_mouthUpperLipRaiseR": lambda: CHEEK_RAISE * 0.5,
    "CTRL_expressions_mouthDimpleL": lambda: CHEEK_RAISE,
    "CTRL_expressions_mouthDimpleR": lambda: CHEEK_RAISE,
}

# Zero baked speech / viseme mouth motion on EyeFixed copies.
# ACE owns live lipsync; AS library should only keep body + non-speech face expression.
STRIP_SPEECH_LIP_MOTION = True

# Kept by frown/smile passes (not treated as speech visemes).
SPEECH_LIP_KEEP_CURVES = {
    "CTRL_expressions_mouthCornerPullL",
    "CTRL_expressions_mouthCornerPullR",
    "CTRL_expressions_mouthCornerDepressL",
    "CTRL_expressions_mouthCornerDepressR",
    "CTRL_expressions_mouthDimpleL",
    "CTRL_expressions_mouthDimpleR",
    "CTRL_expressions_mouthUpperLipRaiseL",
    "CTRL_expressions_mouthUpperLipRaiseR",
}

# Prefixes under CTRL_expressions_* that are speech articulators / visemes.
SPEECH_LIP_PREFIXES = (
    "jaw",
    "tongue",
    "teeth",
    "mouthClose",
    "mouthFunnel",
    "mouthPucker",
    "mouthStretch",
    "mouthPress",
    "mouthShrug",
    "mouthRoll",
    "mouthUpperUp",
    "mouthLowerDown",
    "mouthLeft",
    "mouthRight",
    "mouthSmile",
    "mouthFrown",
    "mouthWiden",
)

_lines: list[str] = []


def log(msg: str) -> None:
    _lines.append(msg)
    unreal.log(f"[EyeGazeOffset] {msg}")


def write_report(ok: bool) -> None:
    path = unreal.Paths.convert_relative_path_to_full(
        unreal.Paths.project_saved_dir() + REPORT
    )
    header = "RESULT: PASS\n" if ok else "RESULT: FAIL\n"
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(header + "\n".join(_lines) + "\n")
    log(f"Report: {path}")


def project_root() -> Path:
    return Path(unreal.Paths.project_dir()).resolve()


def as_path(stem: str) -> str:
    return f"{LIBRARY}/AS_{stem}"


def eye_fixed_path(stem: str) -> str:
    return f"{LIBRARY}/AS_{stem}{OUTPUT_SUFFIX}"


def clamp(value: float) -> float:
    return max(CLAMP_MIN, min(CLAMP_MAX, float(value)))


def mean(values) -> float:
    vals = list(values)
    if not vals:
        return 0.0
    return sum(float(v) for v in vals) / float(len(vals))


def ensure_output_from_source(source_path: str, output_path: str) -> unreal.AnimSequence:
    if not unreal.EditorAssetLibrary.does_asset_exist(source_path):
        raise RuntimeError(f"Source missing: {source_path}")

    if ALWAYS_COPY_FROM_SOURCE or not unreal.EditorAssetLibrary.does_asset_exist(output_path):
        if DRY_RUN:
            log(f"DRY_RUN: would duplicate {source_path} -> {output_path}")
            return unreal.load_asset(source_path)

        if unreal.EditorAssetLibrary.does_asset_exist(output_path):
            unreal.EditorAssetLibrary.delete_asset(output_path)

        # UE: duplicate_asset(source_asset_path, destination_asset_path)
        duplicated = unreal.EditorAssetLibrary.duplicate_asset(source_path, output_path)
        if not duplicated:
            raise RuntimeError(f"Failed to duplicate {source_path} -> {output_path}")
        log(f"Copied source -> {output_path}")

    asset = unreal.load_asset(output_path)
    if not isinstance(asset, unreal.AnimSequence):
        raise RuntimeError(f"Not an AnimSequence: {output_path}")
    return asset


class CurveEditBracket:
    """Batch data-model notifies for one asset.

    Without a bracket the controller recompresses the whole sequence after *every key*, which is
    ~25s per curve on the long clips. Falls back to unbracketed edits if the API is unavailable.
    """

    def __init__(self, seq: unreal.AnimSequence, title: str):
        self.title = title
        # UE 5.8 exposes the controller as a property on AnimSequence, not a getter.
        self.controller = getattr(seq, "controller", None)
        if self.controller is None:
            try:
                self.controller = seq.get_editor_property("controller")
            except Exception as exc:
                log(f"  (no data-model controller: {exc}; edits will be slow)")

    def __enter__(self):
        if self.controller is None:
            return self
        try:
            self.controller.open_bracket(self.title, False)
        except Exception:
            try:
                self.controller.open_bracket(unreal.Text(self.title), False)
            except Exception as exc:
                log(f"  (could not open edit bracket: {exc}; edits will be slow)")
                self.controller = None
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        if self.controller is None:
            return False
        try:
            self.controller.close_bracket(False)
        except Exception:
            try:
                self.controller.close_bracket()
            except Exception as exc:
                log(f"  (could not close edit bracket: {exc})")
        return False


def list_float_curve_names(seq: unreal.AnimSequence) -> list[str]:
    lib = unreal.AnimationLibrary
    names = lib.get_animation_curve_names(seq, unreal.RawCurveTrackTypes.RCT_FLOAT)
    return [str(n) for n in names]


def replace_float_curve(
    seq: unreal.AnimSequence, curve_name: str, times: list[float], values: list[float]
) -> None:
    lib = unreal.AnimationLibrary
    curve_type = unreal.RawCurveTrackTypes.RCT_FLOAT
    name = unreal.Name(curve_name)

    if lib.does_curve_exist(seq, name, curve_type):
        lib.remove_curve(seq, name, False)
    lib.add_curve(seq, name, curve_type, False)

    ue_times = unreal.Array(float)
    ue_values = unreal.Array(float)
    for t, v in zip(times, values):
        ue_times.append(float(t))
        ue_values.append(float(v))
    lib.add_float_curve_keys(seq, name, ue_times, ue_values)


def _read_curve(seq: unreal.AnimSequence, curve_name: str):
    lib = unreal.AnimationLibrary
    name = unreal.Name(curve_name)
    curve_type = unreal.RawCurveTrackTypes.RCT_FLOAT
    if not lib.does_curve_exist(seq, name, curve_type):
        return None
    times, values = lib.get_float_keys(seq, name)
    return [float(t) for t in times], [float(v) for v in values]


def offset_curve(seq: unreal.AnimSequence, curve_name: str, delta: float) -> bool:
    if abs(delta) < 1e-8:
        return False

    read = _read_curve(seq, curve_name)
    if read is None:
        log(f"  skip missing curve: {curve_name}")
        return False

    times_list, old_values = read
    new_values = [clamp(v + delta) for v in old_values]

    log(
        f"  {curve_name}: delta={delta:+.3f}  "
        f"mean {mean(old_values):.3f} -> {mean(new_values):.3f}  "
        f"keys={len(old_values)}"
    )

    if DRY_RUN:
        return True

    replace_float_curve(seq, curve_name, times_list, new_values)
    return True


def sample_curve(times: list[float], values: list[float], time: float) -> float:
    """Linear sample with held ends; empty curve reads as neutral 0."""
    if not times:
        return 0.0
    if time <= times[0]:
        return values[0]
    if time >= times[-1]:
        return values[-1]
    index = bisect.bisect_left(times, time)
    prev_t, next_t = times[index - 1], times[index]
    span = next_t - prev_t
    if span <= 0.0:
        return values[index]
    ratio = (time - prev_t) / span
    return values[index - 1] + (values[index] - values[index - 1]) * ratio


def merge_times(*time_lists: list[float]) -> list[float]:
    """Union of key times, collapsing near-duplicates so paired curves do not double in size."""
    merged: list[float] = []
    for time in sorted(t for times in time_lists for t in times):
        if not merged or time - merged[-1] > 1e-5:
            merged.append(time)
    return merged


def offset_eye_pair(seq: unreal.AnimSequence, curve_base: str, delta: float) -> int:
    """Apply one delta to both eyes, seeding a side the capture left unkeyed.

    A curve authored with zero keys used to silently skip the offset, leaving that eye on the
    uncorrected gaze while the other eye moved — a visible wall-eye (AS_FarewellFinishSpeaking_01).
    """
    if abs(delta) < 1e-8:
        return 0

    sides = {side: _read_curve(seq, f"{curve_base}{side}") for side in ("L", "R")}
    if all(read is None for read in sides.values()):
        log(f"  skip missing curve pair: {curve_base}L/R")
        return 0

    shared_times: list[float] = []
    for read in sides.values():
        if read and read[0]:
            shared_times = read[0]
            break
    if not shared_times:
        log(f"  skip unkeyed curve pair: {curve_base}L/R (neither eye is keyed — stays symmetric)")
        return 0

    changed = 0
    for side, read in sides.items():
        curve_name = f"{curve_base}{side}"
        times_list, old_values = read if read else ([], [])
        if not times_list:
            times_list = list(shared_times)
            old_values = [0.0] * len(times_list)
            log(f"  {curve_name}: unkeyed — seeding {len(times_list)} keys from the other eye")

        new_values = [clamp(v + delta) for v in old_values]
        log(
            f"  {curve_name}: delta={delta:+.3f}  "
            f"mean {mean(old_values):.3f} -> {mean(new_values):.3f}  "
            f"keys={len(old_values)}"
        )
        if not DRY_RUN:
            replace_float_curve(seq, curve_name, times_list, new_values)
        changed += 1
    return changed


def balance_curve_pair(seq: unreal.AnimSequence, curve_base: str) -> int:
    """Give both eyes the average of the two captured sides."""
    read_l = _read_curve(seq, f"{curve_base}L")
    read_r = _read_curve(seq, f"{curve_base}R")
    times_l, values_l = read_l if read_l else ([], [])
    times_r, values_r = read_r if read_r else ([], [])

    times = merge_times(times_l, times_r)
    if not times:
        return 0

    averaged = [
        0.5 * (sample_curve(times_l, values_l, t) + sample_curve(times_r, values_r, t)) for t in times
    ]
    log(
        f"  {curve_base}L/R: balanced  mean L {mean(values_l):.3f} / R {mean(values_r):.3f} "
        f"-> {mean(averaged):.3f}  keys={len(times)}"
    )

    if DRY_RUN:
        return 2

    replace_float_curve(seq, f"{curve_base}L", times, averaged)
    replace_float_curve(seq, f"{curve_base}R", times, averaged)
    return 2


def mirror_curve_pair_from_right(seq: unreal.AnimSequence, curve_base: str) -> int:
    """Copy the right eye's curve onto the left, treating the right as the correct side."""
    read_r = _read_curve(seq, f"{curve_base}R")
    read_l = _read_curve(seq, f"{curve_base}L")
    values_l = read_l[1] if read_l else []

    if not read_r or not read_r[0]:
        # Right is the reference: if it is unkeyed, force left to rest so it cannot diverge alone.
        if read_l and read_l[0]:
            times_l, old_l = read_l
            zeros = [0.0] * len(times_l)
            log(
                f"  {curve_base}L: R unkeyed — zeroing L  mean {mean(old_l):.3f} -> 0.000  "
                f"keys={len(times_l)}"
            )
            if not DRY_RUN:
                replace_float_curve(seq, f"{curve_base}L", list(times_l), zeros)
            return 1
        log(f"  {curve_base}R: unkeyed — left side left as captured")
        return 0

    times_r, values_r = read_r
    log(
        f"  {curve_base}L: mirrored from R  mean {mean(values_l):.3f} -> {mean(values_r):.3f}  "
        f"keys={len(times_r)}"
    )

    if DRY_RUN:
        return 1

    replace_float_curve(seq, f"{curve_base}L", list(times_r), list(values_r))
    return 1


def sequence_length(seq: unreal.AnimSequence) -> float:
    try:
        length = float(unreal.AnimationLibrary.get_sequence_length(seq))
    except Exception:
        length = 0.0
    if length <= 0.0:
        try:
            length = float(seq.get_play_length())
        except Exception:
            length = 0.0
    return length


def force_parallel_look_direction(seq: unreal.AnimSequence) -> int:
    """Key the MetaHuman control that makes both eyeballs share one look direction."""
    length = sequence_length(seq)
    if length <= 0.0:
        log(f"  skip {PARALLEL_LOOK_DIRECTION_CURVE}: sequence length unknown")
        return 0

    log(
        f"  {PARALLEL_LOOK_DIRECTION_CURVE}: set to {PARALLEL_LOOK_DIRECTION_VALUE:.2f} "
        f"over 0..{length:.2f}s"
    )
    if DRY_RUN:
        return 1

    replace_float_curve(
        seq,
        PARALLEL_LOOK_DIRECTION_CURVE,
        [0.0, length],
        [PARALLEL_LOOK_DIRECTION_VALUE, PARALLEL_LOOK_DIRECTION_VALUE],
    )
    return 1


def scale_curve(
    seq: unreal.AnimSequence, curve_name: str, scale: float, extra_subtract: float = 0.0
) -> bool:
    """Scale curve values toward neutral, then optionally subtract more."""
    if abs(scale - 1.0) < 1e-8 and abs(extra_subtract) < 1e-8:
        return False

    read = _read_curve(seq, curve_name)
    if read is None:
        log(f"  skip missing curve: {curve_name}")
        return False

    times_list, old_values = read
    new_values = [clamp(v * scale - extra_subtract) for v in old_values]

    log(
        f"  {curve_name}: scale={scale:.3f} relax={extra_subtract:.3f}  "
        f"mean {mean(old_values):.3f} -> {mean(new_values):.3f}  "
        f"keys={len(old_values)}"
    )

    if DRY_RUN:
        return True

    replace_float_curve(seq, curve_name, times_list, new_values)
    return True


def frown_curve_list() -> list[str]:
    names = list(FROWN_CORE_CURVES)
    if RELAX_BROW_LATERAL:
        names.extend(FROWN_LATERAL_CURVES)
    if RELAX_NOSE_WRINKLE:
        names.extend(FROWN_WRINKLE_CURVES)
    return names


def is_speech_lip_curve(curve_name: str) -> bool:
    """True for jaw/tongue/teeth/viseme mouth curves; false for soft-smile / frown keep-list."""
    if curve_name in SPEECH_LIP_KEEP_CURVES:
        return False
    prefix = "CTRL_expressions_"
    if not curve_name.startswith(prefix):
        return False
    rest = curve_name[len(prefix) :]
    return any(rest.startswith(p) for p in SPEECH_LIP_PREFIXES)


def strip_speech_lip_curves(seq: unreal.AnimSequence, available: set[str]) -> int:
    """Flatten baked speaking mouth motion so ACE alone drives lipsync."""
    if not STRIP_SPEECH_LIP_MOTION:
        return 0
    changed = 0
    speech_present = sorted(n for n in available if is_speech_lip_curve(n))
    log(
        f"Speech-lip curves to zero: {speech_present if speech_present else '(none)'}"
    )
    for curve_name in speech_present:
        if scale_curve(seq, curve_name, 0.0, 0.0):
            changed += 1
    return changed


def apply_offsets(seq: unreal.AnimSequence) -> int:
    changed = 0
    available = set(list_float_curve_names(seq))
    eye_names = [n for n in available if "eyelook" in n.lower()]
    frown_present = [n for n in frown_curve_list() if n in available]
    log(f"Eye-related curves on asset: {eye_names if eye_names else '(none)'}")
    log(f"Frown-related curves on asset: {frown_present if frown_present else '(none)'}")

    # ACE owns live lips — remove baked visemes before expression knobs.
    changed += strip_speech_lip_curves(seq, available)

    for curve_base, delta_fn in EYE_PAIR_DELTAS.items():
        changed += offset_eye_pair(seq, curve_base, float(delta_fn()))

    if BALANCE_EYE_CURVES:
        for curve_base in BALANCE_CURVE_BASES:
            changed += balance_curve_pair(seq, curve_base)

    if MIRROR_RIGHT_ONTO_LEFT:
        for curve_base in MIRROR_CURVE_BASES:
            changed += mirror_curve_pair_from_right(seq, curve_base)

    if FORCE_PARALLEL_LOOK_DIRECTION:
        changed += force_parallel_look_direction(seq)

    for curve_name in frown_curve_list():
        if scale_curve(seq, curve_name, float(FROWN_SCALE), float(FROWN_RELAX)):
            changed += 1

    for curve_name, delta_fn in SMILE_CURVE_DELTAS.items():
        if offset_curve(seq, curve_name, float(delta_fn())):
            changed += 1
    return changed


def process_stem(stem: str) -> bool:
    source = as_path(stem)
    output = eye_fixed_path(stem)
    log(f"--- {stem} ---")
    log(f"Source: {source}")
    log(f"Output: {output}")
    log(
        f"Knobs: GAZE_LIFT={GAZE_LIFT} FROWN_SCALE={FROWN_SCALE} FROWN_RELAX={FROWN_RELAX} "
        f"SMILE_AMOUNT={SMILE_AMOUNT} CHEEK_RAISE={CHEEK_RAISE} "
        f"STRIP_SPEECH_LIP_MOTION={STRIP_SPEECH_LIP_MOTION} "
        f"DOWN_EXTRA={DOWN_EXTRA} UP_EXTRA={UP_EXTRA} "
        f"LEFT_EXTRA={LEFT_EXTRA} RIGHT_EXTRA={RIGHT_EXTRA} "
        f"BALANCE_EYE_CURVES={BALANCE_EYE_CURVES} "
        f"MIRROR_RIGHT_ONTO_LEFT={MIRROR_RIGHT_ONTO_LEFT} "
        f"FORCE_PARALLEL_LOOK_DIRECTION={FORCE_PARALLEL_LOOK_DIRECTION} DRY_RUN={DRY_RUN}"
    )

    seq = ensure_output_from_source(source, output)
    with CurveEditBracket(seq, f"Eye correction {stem}"):
        changed = apply_offsets(seq)

    if DRY_RUN:
        log(f"DRY_RUN complete ({changed} curves would change)")
        return True

    saved = unreal.EditorAssetLibrary.save_asset(output)
    log(f"Saved={saved} changed_curves={changed}")
    return bool(saved) and changed >= 0


def stems_from_plan() -> list[str]:
    path = project_root() / PLAN_PATH
    if not path.is_file():
        raise RuntimeError(f"Plan missing: {path}")
    data = json.loads(path.read_text(encoding="utf-8"))
    stems: list[str] = []
    for item in data.get("items", []):
        cue = item.get("cue_id")
        status = item.get("status")
        if not cue:
            continue
        if status in ("missing_source_as", "not_in_catalog"):
            continue
        if unreal.EditorAssetLibrary.does_asset_exist(as_path(cue)):
            stems.append(cue)
    return stems


def stems_from_library() -> list[str]:
    """Every AS_* in LIBRARY that is not already an *_EyeFixed asset."""
    asset_paths = unreal.EditorAssetLibrary.list_assets(LIBRARY, recursive=False, include_folder=False)
    stems: list[str] = []
    for path in asset_paths:
        name = path.rsplit("/", 1)[-1].split(".")[0]
        if not name.startswith("AS_"):
            continue
        if name.endswith(OUTPUT_SUFFIX):
            continue
        stem = name[3:]  # strip AS_
        if unreal.EditorAssetLibrary.does_asset_exist(as_path(stem)):
            stems.append(stem)
    stems.sort()
    return stems


def main() -> None:
    log("MetaHuman eye + frown + speech-lip curve correction")
    if BATCH_ALL_IN_LIBRARY:
        stems = stems_from_library()
        log(f"Batch library mode: {len(stems)} AS_* stems under {LIBRARY}")
    elif BATCH_FROM_PLAN:
        stems = stems_from_plan()
        log(f"Batch plan mode: {len(stems)} stems from plan")
    else:
        stems = [SOURCE_STEM]

    ok = True
    for stem in stems:
        try:
            if not process_stem(stem):
                ok = False
        except Exception as exc:
            ok = False
            log(f"ERROR on {stem}: {exc}")

    write_report(ok)
    if not ok:
        raise RuntimeError("Eye gaze offset finished with errors — see report.")


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        unreal.log_error(f"[EyeGazeOffset] {exc}")
        write_report(False)
        raise
