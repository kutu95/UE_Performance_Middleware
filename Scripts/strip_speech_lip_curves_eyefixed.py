"""Fast pass: zero baked speech/viseme mouth curves on existing AS_*_EyeFixed assets.

Does NOT rebuild from source (no delete/duplicate). Safe to run when the full
offset_eye_gaze_curves.py batch is too slow or appears hung.

In Unreal: Tools -> Execute Python Script -> this file
"""
from __future__ import annotations

import unreal

LIBRARY = "/Game/Godfrey/Animation/Animation/Performances"
OUTPUT_SUFFIX = "_EyeFixed"
DRY_RUN = False
REPORT = "SpeechLipStrip.txt"

# Optional: only these stems (without AS_ / _EyeFixed). Empty = all *_EyeFixed in library.
ONLY_STEMS: list[str] = []

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
    unreal.log(f"[SpeechLipStrip] {msg}")
    # Flush progress so a long run never looks hung with an empty report.
    path = unreal.Paths.convert_relative_path_to_full(
        unreal.Paths.project_saved_dir() + REPORT
    )
    with open(path, "w", encoding="utf-8") as handle:
        handle.write("RESULT: IN_PROGRESS\n" + "\n".join(_lines) + "\n")


def is_speech_lip_curve(curve_name: str) -> bool:
    if curve_name in SPEECH_LIP_KEEP_CURVES:
        return False
    prefix = "CTRL_expressions_"
    if not curve_name.startswith(prefix):
        return False
    rest = curve_name[len(prefix) :]
    return any(rest.startswith(p) for p in SPEECH_LIP_PREFIXES)


def zero_curve(seq: unreal.AnimSequence, curve_name: str) -> bool:
    lib = unreal.AnimationLibrary
    name = unreal.Name(curve_name)
    curve_type = unreal.RawCurveTrackTypes.RCT_FLOAT
    if not lib.does_curve_exist(seq, name, curve_type):
        return False
    keys = lib.get_float_keys(seq, name)
    times = list(keys.times)
    if not times:
        return False
    if DRY_RUN:
        log(f"  DRY would zero {curve_name} ({len(times)} keys)")
        return True
    lib.remove_curve(seq, name, False)
    lib.add_curve(seq, name, curve_type, False)
    ue_times = unreal.Array(float)
    ue_values = unreal.Array(float)
    for t in times:
        ue_times.append(float(t))
        ue_values.append(0.0)
    lib.add_float_curve_keys(seq, name, ue_times, ue_values)
    return True


def list_eye_fixed_paths() -> list[str]:
    asset_paths = unreal.EditorAssetLibrary.list_assets(
        LIBRARY, recursive=False, include_folder=False
    )
    out: list[str] = []
    for path in asset_paths:
        name = path.rsplit("/", 1)[-1].split(".")[0]
        if not name.startswith("AS_") or not name.endswith(OUTPUT_SUFFIX):
            continue
        stem = name[3 : -len(OUTPUT_SUFFIX)]
        if ONLY_STEMS and stem not in ONLY_STEMS:
            continue
        out.append(path.split(".")[0])
    out.sort()
    return out


def process_asset(asset_path: str) -> int:
    seq = unreal.load_asset(asset_path)
    if not isinstance(seq, unreal.AnimSequence):
        log(f"Skip (not AnimSequence): {asset_path}")
        return 0
    lib = unreal.AnimationLibrary
    names = [str(n) for n in lib.get_animation_curve_names(seq, unreal.RawCurveTrackTypes.RCT_FLOAT)]
    speech = sorted(n for n in names if is_speech_lip_curve(n))
    log(f"--- {asset_path} ({len(speech)} speech-lip curves) ---")
    changed = 0
    for curve_name in speech:
        if zero_curve(seq, curve_name):
            changed += 1
    if changed and not DRY_RUN:
        saved = unreal.EditorAssetLibrary.save_asset(asset_path)
        log(f"Saved={saved} zeroed={changed}")
    else:
        log(f"zeroed={changed} DRY_RUN={DRY_RUN}")
    return changed


def main() -> None:
    log("Fast speech-lip strip on existing *_EyeFixed (no rebuild from source)")
    paths = list_eye_fixed_paths()
    log(f"Assets to process: {len(paths)}")
    total = 0
    for i, path in enumerate(paths, start=1):
        log(f"[{i}/{len(paths)}]")
        total += process_asset(path)
    path = unreal.Paths.convert_relative_path_to_full(
        unreal.Paths.project_saved_dir() + REPORT
    )
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(f"RESULT: PASS\ntotal_curves_zeroed={total}\n" + "\n".join(_lines) + "\n")
    unreal.log(f"[SpeechLipStrip] DONE total_curves_zeroed={total} report={path}")


main()
