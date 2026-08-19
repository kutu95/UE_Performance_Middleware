"""Make Godfrey's left baked eye material match the right (UE 5.8).

MI_EyeL_Baked carries five scalar overrides that MI_EyeR_Baked does not:
Cloudy Eye Intensity/Size/Softness/Variation and Melanosis Ring Opacity
Variation Rotation. They are currently inert (Use Cloudy Eye and
Use Limbal Melanosis Ring are both off), but they mean the two eyes would
diverge the moment either switch is enabled.

The right eye is the reference. For each parameter this drops the left
override so both sides inherit the same value; if the inherited value still
does not match the right eye, an explicit override equal to the right eye's
effective value is written instead.

Writes Saved/GodfreyEyeMaterialFix.txt. Re-run audit_godfrey_eye_material_symmetry.py
afterwards to confirm.

Headless (copy to a space-free path first, UE splits the arg on "UE Projects"):
  UnrealEditor-Cmd.exe "D:/UE Projects/MetaHuman_Baseline_UE58_Test/UnrealPerformer.uproject"
    -ExecutePythonScript="D:/godfrey_eye_fix_tmp.py"
    -unattended -nop4 -nosplash -NullRHI -log
"""
from __future__ import annotations

import unreal

EYE_L = "/Game/MetaHumans/MHC_CaptainGodfrey/Face/Materials/MI_EyeL_Baked"
EYE_R = "/Game/MetaHumans/MHC_CaptainGodfrey/Face/Materials/MI_EyeR_Baked"

# Left-only overrides to reconcile against the right eye.
PARAMETERS = [
    "Cloudy Eye Intensity",
    "Cloudy Eye Size",
    "Cloudy Eye Softness",
    "Cloudy Eye Variation",
    "Melanosis Ring Opacity Variation Rotation",
]

TOLERANCE = 1e-4

REPORT_TXT = "GodfreyEyeMaterialFix.txt"

_lines: list[str] = []


def log(msg: str) -> None:
    _lines.append(msg)
    unreal.log(f"[EyeFix] {msg}")


def saved_path(filename: str) -> str:
    return unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir() + filename)


def effective_scalar(mi, name: str):
    try:
        return float(unreal.MaterialEditingLibrary.get_material_instance_scalar_parameter_value(
            mi, unreal.Name(name)))
    except Exception:
        return None


def drop_overrides(mi, names: set[str]) -> int:
    kept = []
    removed = 0
    for entry in mi.get_editor_property("scalar_parameter_values") or []:
        info = entry.get_editor_property("parameter_info")
        if str(info.get_editor_property("name")) in names:
            removed += 1
            continue
        kept.append(entry)
    if removed:
        mi.set_editor_property("scalar_parameter_values", kept)
    return removed


def main() -> None:
    left = unreal.EditorAssetLibrary.load_asset(EYE_L)
    right = unreal.EditorAssetLibrary.load_asset(EYE_R)
    if left is None or right is None:
        log("RESULT: FAIL - could not load both eye material instances")
        return

    reference = {name: effective_scalar(right, name) for name in PARAMETERS}
    before = {name: effective_scalar(left, name) for name in PARAMETERS}

    log("=== Before ===")
    for name in PARAMETERS:
        log(f"  {name:<44} L={before[name]!s:<12} R(reference)={reference[name]}")
    log("")

    removed = drop_overrides(left, set(PARAMETERS))
    unreal.MaterialEditingLibrary.update_material_instance(left)
    log(f"Dropped {removed} left-eye override(s) so they inherit from the parent chain.")

    # Inheritance may not land on the right eye's value; pin anything still off.
    pinned = 0
    for name in PARAMETERS:
        target = reference[name]
        current = effective_scalar(left, name)
        if target is None or current is None:
            log(f"  ! could not resolve '{name}' (L={current}, R={target})")
            continue
        if abs(current - target) > TOLERANCE:
            unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(
                left, unreal.Name(name), target)
            pinned += 1
            log(f"  pinned '{name}' to right-eye value {target:.5f} (inherited {current:.5f})")
    if pinned:
        unreal.MaterialEditingLibrary.update_material_instance(left)

    log("")
    log("=== After ===")
    mismatches = 0
    for name in PARAMETERS:
        lv, rv = effective_scalar(left, name), reference[name]
        ok = lv is not None and rv is not None and abs(lv - rv) <= TOLERANCE
        mismatches += 0 if ok else 1
        log(f"  {name:<44} L={lv!s:<12} R={rv!s:<12} {'ok' if ok else 'MISMATCH'}")

    saved = unreal.EditorAssetLibrary.save_asset(EYE_L, only_if_is_dirty=False)
    log("")
    log(f"Saved {EYE_L}: {saved}")
    if not saved:
        log("Save failed. If the editor is open, the .uasset is locked (MoveFile error 32) and a")
        log("headless commandlet cannot overwrite it — run this from Tools > Execute Python Script")
        log("instead, or close the editor and re-run headless.")

    if mismatches:
        log(f"RESULT: FAIL ({mismatches} parameter(s) still differ from the right eye)")
    elif not saved:
        log("RESULT: FAIL (values corrected in memory but the asset could not be written)")
    else:
        log("RESULT: PASS (left eye now matches right; asset saved)")

    with open(saved_path(REPORT_TXT), "w", encoding="utf-8") as handle:
        handle.write("\n".join(_lines) + "\n")
    unreal.log(f"[EyeFix] wrote {saved_path(REPORT_TXT)}")


main()
