"""Generic property diff of Godfrey's two baked eye material instances (UE 5.8).

The parameter-level audit (audit_godfrey_eye_material_symmetry.py) only compares
named material parameters. This walks every exposed editor property on both
UMaterialInstanceConstant objects - subsurface profile, base property overrides,
blend mode, shading model, nanite override, etc. - so nothing is missed.

Read-only. Writes Saved/GodfreyEyeInstanceDiff.txt.

Headless (copy to a space-free path first, UE splits the arg on "UE Projects"):
  UnrealEditor-Cmd.exe "D:/UE Projects/MetaHuman_Baseline_UE58_Test/UnrealPerformer.uproject"
    -ExecutePythonScript="D:/godfrey_eye_diff_tmp.py"
    -unattended -nop4 -nosplash -NullRHI -log
"""
from __future__ import annotations

import unreal

EYE_L = "/Game/MetaHumans/MHC_CaptainGodfrey/Face/Materials/MI_EyeL_Baked"
EYE_R = "/Game/MetaHumans/MHC_CaptainGodfrey/Face/Materials/MI_EyeR_Baked"

# Parameter arrays are covered by the other audit; skip the noise here.
SKIP = {
    "scalar_parameter_values",
    "vector_parameter_values",
    "texture_parameter_values",
    "runtime_virtual_texture_parameter_values",
    "sparse_volume_texture_parameter_values",
    "font_parameter_values",
    "static_parameters",
    "parent",
}

REPORT_TXT = "GodfreyEyeInstanceDiff.txt"

_lines: list[str] = []


def log(msg: str) -> None:
    _lines.append(msg)
    unreal.log(f"[EyeDiff] {msg}")


def saved_path(filename: str) -> str:
    return unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir() + filename)


def property_names(obj) -> list[str]:
    """Editor property names, derived from the generated Python type stub."""
    names = set()
    for attr in dir(type(obj)):
        if attr.startswith("_"):
            continue
        try:
            if isinstance(getattr(type(obj), attr), property):
                names.add(attr)
        except Exception:
            continue
    return sorted(names)


def read(obj, name):
    try:
        value = obj.get_editor_property(name)
    except Exception as exc:
        return f"<error: {exc}>"
    if isinstance(value, unreal.Object):
        return value.get_path_name()
    return value


def main() -> None:
    left = unreal.EditorAssetLibrary.load_asset(EYE_L)
    right = unreal.EditorAssetLibrary.load_asset(EYE_R)
    if left is None or right is None:
        log("RESULT: FAIL - could not load both instances")
        return

    log(f"L: {EYE_L}  ({type(left).__name__})")
    log(f"R: {EYE_R}  ({type(right).__name__})")
    log("")

    names = property_names(left)
    diffs = 0
    log(f"{'property':<44} {'LEFT':<40} {'RIGHT':<40}")
    for name in names:
        if name in SKIP:
            continue
        lv, rv = read(left, name), read(right, name)
        ltxt, rtxt = str(lv), str(rv)
        if ltxt == rtxt:
            continue
        diffs += 1
        log(f"{name:<44} {ltxt:<40} {rtxt:<40}  <== DIFFERS")

    log("")
    log("=== Subsurface / base property detail ===")
    for label, mi in (("L", left), ("R", right)):
        log(f"{label} subsurface_profile     : {read(mi, 'subsurface_profile')}")
        log(f"{label} base_property_overrides: {read(mi, 'base_property_overrides')}")
        log(f"{label} parent                 : {read(mi, 'parent')}")

    log("")
    log(f"RESULT: {diffs} differing editor propert{'y' if diffs == 1 else 'ies'} "
        f"(excluding parameter arrays and parent)")

    with open(saved_path(REPORT_TXT), "w", encoding="utf-8") as handle:
        handle.write("\n".join(_lines) + "\n")
    unreal.log(f"[EyeDiff] wrote {saved_path(REPORT_TXT)}")


main()
