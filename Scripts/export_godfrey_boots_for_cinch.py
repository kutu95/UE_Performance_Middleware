"""Export Godfrey's worn outfit + Loose_Biker_Boots textures for a Blender cinch.

Primary FBX is the assembled MHC outfit (the mesh on the character), so boot
verts already match the pants hem. Standalone boot pieces are extras.

After this:
  Scripts/run_blender_cinch_godfrey_boots.ps1
  Tools → Execute Python Script → Scripts/apply_godfrey_cinched_boots.py

Tools → Execute Python Script (editor open).
"""
from __future__ import annotations

import os
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

import unreal

OUTFIT = "/Game/MetaHumans/MHC_CaptainGodfrey/Clothing/MHC_CaptainGodfrey_Outfits"
BOOT_A = "/Game/Outfits/casual_formal/Meshes/A/SKM_loose_biker_boots1"
BOOT_B = "/Game/Outfits/casual_formal/Meshes/B/SKM_loose_biker_boots1"
TEXTURES = (
    "/Game/Outfits/casual_formal/textures/Loose_Biker_Boots_Diffuse",
    "/Game/Outfits/casual_formal/textures/Loose_Biker_Boots_Normal",
    "/Game/Outfits/casual_formal/textures/Loose_Biker_Boots_ORM",
)
REPORT = "ExportGodfreyBootsForCinch.txt"
_lines: list[str] = []


def log(msg: str) -> None:
    _lines.append(msg)
    unreal.log(f"[BootCinchExport] {msg}")


def project_root() -> str:
    return unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()).rstrip("/\\")


def out_dir() -> str:
    path = os.path.join(project_root(), "Saved", "BootCinch")
    os.makedirs(path, exist_ok=True)
    return path


def write_report(ok: bool) -> None:
    path = unreal.Paths.convert_relative_path_to_full(
        unreal.Paths.project_saved_dir() + REPORT
    )
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(("RESULT: PASS\n" if ok else "RESULT: FAIL\n") + "\n".join(_lines) + "\n")
    log(f"Report: {path}")


def export_fbx(mesh: unreal.SkeletalMesh, out_fbx: str) -> bool:
    task = unreal.AssetExportTask()
    task.object = mesh
    task.filename = out_fbx
    task.automated = True
    task.replace_identical = True
    task.prompt = False
    task.exporter = unreal.SkeletalMeshExporterFBX()
    options = unreal.FbxExportOption()
    for prop, value in (
        ("ascii", False),
        ("level_of_detail", False),
        ("collision", False),
        ("vertex_color", True),
        ("export_morph_targets", False),
        ("export_preview_mesh", True),
        ("map_skeletal_motion_to_root", False),
    ):
        try:
            options.set_editor_property(prop, value)
        except Exception:
            pass
    task.options = options
    ok = bool(unreal.Exporter.run_asset_export_task(task))
    exists = os.path.isfile(out_fbx)
    size = os.path.getsize(out_fbx) if exists else 0
    log(f"  FBX {out_fbx}: ok={ok} exists={exists} bytes={size}")
    return ok and exists and size > 1024


def export_png(tex, out_path: str) -> bool:
    task = unreal.AssetExportTask()
    task.set_editor_property("object", tex)
    task.set_editor_property("filename", out_path)
    task.set_editor_property("automated", True)
    task.set_editor_property("prompt", False)
    task.set_editor_property("replace_identical", True)
    try:
        ok = bool(unreal.Exporter.run_asset_export_task(task))
    except Exception as exc:
        log(f"  PNG fail {tex.get_name()}: {exc}")
        return False
    exists = os.path.isfile(out_path)
    log(f"  PNG {out_path}: ok={ok} exists={exists}")
    return bool(ok) and exists


def write_notes(folder: str) -> None:
    path = os.path.join(folder, "NOTES.txt")
    text = """Godfrey boot cinch
==================

Goal: same Loose_Biker_Boots, collars closed, tongue in, laces looking done-up.
Coat, pants, and cotton tank stay.

1. This folder already has the Unreal FBX export (if the export script passed).
2. Run:  Scripts/run_blender_cinch_godfrey_boots.ps1
   (needs Blender 3.6+ or 4.x on PATH or in Program Files)
3. Back in Unreal: Tools → Execute Python Script → Scripts/apply_godfrey_cinched_boots.py

Primary mesh: MHC_CaptainGodfrey_Outfits.fbx
The script only moves Loose_Biker_Boots vertices. Coat / tank / pants are untouched.

Manual Blender (if you skip the script):
- Import the Outfits FBX
- Select the clothing mesh, edit mode, select by material Loose_Biker_Boots
- Scale the upper collar toward each shin (leave the soles)
- Export FBX, same armature, no leaf bones, do not apply the Armature modifier
- Save as MHC_CaptainGodfrey_Outfits_CinchedBoots.fbx in this folder
"""
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(text)
    log(f"Wrote {path}")


def main() -> None:
    log("=== Export Godfrey boots for Blender cinch ===")
    folder = out_dir()
    log(f"Out: {folder}")
    write_notes(folder)

    exported = 0
    for asset_path, filename in (
        (OUTFIT, "MHC_CaptainGodfrey_Outfits.fbx"),
        (BOOT_A, "SKM_loose_biker_boots1_A.fbx"),
        (BOOT_B, "SKM_loose_biker_boots1_B.fbx"),
    ):
        mesh = unreal.load_asset(asset_path)
        if not isinstance(mesh, unreal.SkeletalMesh):
            log(f"WARN missing {asset_path}")
            continue
        skel = None
        try:
            skel = mesh.get_editor_property("skeleton")
        except Exception:
            pass
        log(f"Mesh {mesh.get_name()} skeleton={skel.get_path_name() if skel else '?'}")
        if export_fbx(mesh, os.path.join(folder, filename)):
            exported += 1

    for tex_path in TEXTURES:
        tex = unreal.load_asset(tex_path)
        if not tex:
            log(f"WARN missing {tex_path}")
            continue
        png = os.path.join(folder, tex.get_name() + ".png")
        export_png(tex, png)

    primary = os.path.join(folder, "MHC_CaptainGodfrey_Outfits.fbx")
    ok = os.path.isfile(primary) and os.path.getsize(primary) > 1024
    log(f"Exported skeletal meshes: {exported}")
    write_report(ok)
    if not ok:
        raise RuntimeError("Primary outfit FBX did not export")
    log("PASS — run Scripts/run_blender_cinch_godfrey_boots.ps1 next.")


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        unreal.log_error(f"[BootCinchExport] {exc}")
        write_report(False)
        raise
