"""Audit per-LOD material mapping on Godfrey's face mesh (UE 5.8).

If a LOD's lod_material_map remaps the eyeLeft/eyeRight sections to different
material slots, one eye renders with another material at distance even though the
two eye material instances themselves are identical.

Also dumps the shared eye-adjacent materials (eye shell, lacrimal fluid,
eyelashes) so any left/right asymmetry hiding in those is visible.

Read-only. Writes Saved/GodfreyFaceLodMaterials.txt.

Headless (copy to a space-free path first, UE splits the arg on "UE Projects"):
  UnrealEditor-Cmd.exe "D:/UE Projects/MetaHuman_Baseline_UE58_Test/UnrealPerformer.uproject"
    -ExecutePythonScript="D:/godfrey_face_lod_tmp.py"
    -unattended -nop4 -nosplash -NullRHI -log
"""
from __future__ import annotations

import unreal

FACE_MESH = "/Game/MetaHumans/MHC_CaptainGodfrey/Face/SKM_MHC_CaptainGodfrey_FaceMesh"

SHARED_EYE_MATERIALS = [
    "/Game/MetaHumans/MHC_CaptainGodfrey/Face/Materials/MI_Face_EyeShell",
    "/Game/MetaHumans/MHC_CaptainGodfrey/Face/Materials/MI_Face_LacrimalFluid",
    "/Game/MetaHumans/MHC_CaptainGodfrey/Face/Materials/MI_Face_Eyelashes",
]

REPORT_TXT = "GodfreyFaceLodMaterials.txt"

_lines: list[str] = []


def log(msg: str) -> None:
    _lines.append(msg)
    unreal.log(f"[FaceLod] {msg}")


def saved_path(filename: str) -> str:
    return unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir() + filename)


def prop(obj, name, default=None):
    try:
        return obj.get_editor_property(name)
    except Exception:
        return default


def main() -> None:
    mesh = unreal.EditorAssetLibrary.load_asset(FACE_MESH)
    if mesh is None:
        log(f"RESULT: FAIL - could not load {FACE_MESH}")
        return

    log("=== Material slots ===")
    slots = prop(mesh, "materials", []) or []
    slot_names = []
    for index, entry in enumerate(slots):
        name = str(prop(entry, "material_slot_name"))
        iface = prop(entry, "material_interface")
        slot_names.append(name)
        log(f"  [{index:>2}] {name:<32} {iface.get_path_name() if iface else 'None'}")
    log("")

    log("=== Per-LOD material remap (lod_material_map) ===")
    log("An empty map means 'section index = slot index' (the normal case).")
    lod_info = prop(mesh, "lod_info", []) or []
    for lod_index, info in enumerate(lod_info):
        remap = list(prop(info, "lod_material_map", []) or [])
        log(f"  LOD{lod_index}: lod_material_map = {remap if remap else '[] (identity)'}")
        for section, target in enumerate(remap):
            if target < 0 or target >= len(slot_names):
                continue
            src = slot_names[section] if section < len(slot_names) else f"section{section}"
            if "eye" in src.lower() or "eye" in slot_names[target].lower():
                flag = "" if section == target else "   <== REMAPPED"
                log(f"        section {section} ({src}) -> slot {target} ({slot_names[target]}){flag}")
    log("")

    log("=== Shared eye-adjacent materials ===")
    for path in SHARED_EYE_MATERIALS:
        mat = unreal.EditorAssetLibrary.load_asset(path)
        if mat is None:
            log(f"  {path}: MISSING")
            continue
        parent = prop(mat, "parent")
        log(f"\n  {path.rsplit('/', 1)[-1]}  (parent {parent.get_path_name() if parent else 'None'})")
        for entry in prop(mat, "texture_parameter_values", []) or []:
            info = prop(entry, "parameter_info")
            tex = prop(entry, "parameter_value")
            log(f"    tex   {str(prop(info, 'name')):<38} {tex.get_path_name() if tex else 'None'}")
        for entry in prop(mat, "scalar_parameter_values", []) or []:
            info = prop(entry, "parameter_info")
            log(f"    scalar {str(prop(info, 'name')):<37} {float(prop(entry, 'parameter_value', 0.0)):.5f}")
        for entry in prop(mat, "vector_parameter_values", []) or []:
            info = prop(entry, "parameter_info")
            col = prop(entry, "parameter_value")
            log(f"    vec   {str(prop(info, 'name')):<38} "
                f"({float(prop(col, 'r', 0)):.4f}, {float(prop(col, 'g', 0)):.4f}, "
                f"{float(prop(col, 'b', 0)):.4f}, {float(prop(col, 'a', 0)):.4f})")

    with open(saved_path(REPORT_TXT), "w", encoding="utf-8") as handle:
        handle.write("\n".join(_lines) + "\n")
    unreal.log(f"[FaceLod] wrote {saved_path(REPORT_TXT)}")


main()
