"""Export Captain Godfrey MetaHuman body (and face) reference for external costume fitting.

Writes FBX + measurement notes into Delivery/Fiverr_Phillip_CaptainCostume/01_BodyReference/

Headless:
  UnrealEditor-Cmd.exe "D:/UE Projects/MetaHuman_Baseline_UE58_Test/UnrealPerformer.uproject"
    -ExecutePythonScript="D:/UE Projects/MetaHuman_Baseline_UE58_Test/Scripts/export_godfrey_body_for_costume.py"
    -unattended -nop4 -nosplash -log
"""
from __future__ import annotations

import json
import os
from datetime import datetime, timezone

import unreal

BODY_PATH = "/Game/MetaHumans/MHC_CaptainGodfrey/Body/SKM_MHC_CaptainGodfrey_BodyMesh"
FACE_PATH = "/Game/MetaHumans/MHC_CaptainGodfrey/Face/SKM_MHC_CaptainGodfrey_FaceMesh"
PHYS_PATH = "/Game/MetaHumans/MHC_CaptainGodfrey/Body/PHYS_MHC_CaptainGodfrey"

DELIVERY_REL = "Delivery/Fiverr_Phillip_CaptainCostume/01_BodyReference"
REPORT_NAME = "ExportGodfreyBodyForCostume.txt"

_lines: list[str] = []


def log(msg: str) -> None:
    _lines.append(msg)
    unreal.log(f"[ExportGodfreyCostumeRef] {msg}")


def project_root() -> str:
    return unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()).rstrip("/\\")


def delivery_dir() -> str:
    path = os.path.join(project_root(), *DELIVERY_REL.split("/"))
    os.makedirs(path, exist_ok=True)
    return path


def write_report(ok: bool) -> None:
    saved = unreal.Paths.convert_relative_path_to_full(
        unreal.Paths.project_saved_dir() + REPORT_NAME
    )
    body = ("RESULT: PASS\n" if ok else "RESULT: FAIL\n") + "\n".join(_lines) + "\n"
    with open(saved, "w", encoding="utf-8") as handle:
        handle.write(body)
    # Mirror into delivery folder for easy packaging.
    mirror = os.path.join(delivery_dir(), REPORT_NAME)
    with open(mirror, "w", encoding="utf-8") as handle:
        handle.write(body)
    log(f"Report: {saved}")


def mesh_bounds(mesh: unreal.SkeletalMesh) -> dict:
    box = mesh.get_bounds()
    origin = box.origin
    extent = box.box_extent
    min_v = unreal.Vector(origin.x - extent.x, origin.y - extent.y, origin.z - extent.z)
    max_v = unreal.Vector(origin.x + extent.x, origin.y + extent.y, origin.z + extent.z)
    height = float(max_v.z - min_v.z)
    width = float(max_v.x - min_v.x)
    depth = float(max_v.y - min_v.y)
    return {
        "origin_cm": [float(origin.x), float(origin.y), float(origin.z)],
        "extent_cm": [float(extent.x), float(extent.y), float(extent.z)],
        "min_cm": [float(min_v.x), float(min_v.y), float(min_v.z)],
        "max_cm": [float(max_v.x), float(max_v.y), float(max_v.z)],
        "height_cm": height,
        "width_cm": width,
        "depth_cm": depth,
    }


def skeleton_name(mesh: unreal.SkeletalMesh) -> str:
    try:
        skel = mesh.get_editor_property("skeleton")
        if skel:
            return skel.get_name()
    except Exception as exc:  # noqa: BLE001
        log(f"skeleton read warning: {exc}")
    return "(unknown)"


def bone_count(mesh: unreal.SkeletalMesh) -> int:
    try:
        # UE5 SkeletalMesh API varies; try common accessors.
        if hasattr(mesh, "get_num_bones"):
            return int(mesh.get_num_bones())
        ref = mesh.get_ref_skeleton() if hasattr(mesh, "get_ref_skeleton") else None
        if ref and hasattr(ref, "get_num"):
            return int(ref.get_num())
    except Exception as exc:  # noqa: BLE001
        log(f"bone count warning: {exc}")
    return -1


def export_fbx(mesh: unreal.SkeletalMesh, out_fbx: str) -> bool:
    task = unreal.AssetExportTask()
    task.object = mesh
    task.filename = out_fbx
    task.automated = True
    task.replace_identical = True
    task.prompt = False
    task.exporter = unreal.SkeletalMeshExporterFBX()

    options = unreal.FbxExportOption()
    # Prefer a clean fitting mesh: geometry + skin weights + skeleton.
    for prop, value in (
        ("ascii", False),
        ("level_of_detail", False),
        ("collision", False),
        ("vertex_color", False),
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
    log(f"Export {out_fbx}: ok={ok} exists={exists} bytes={size}")
    return ok and exists and size > 0


def write_measurements(body: unreal.SkeletalMesh, face: unreal.SkeletalMesh | None, out_json: str, out_txt: str) -> None:
    body_bounds = mesh_bounds(body)
    payload = {
        "exported_utc": datetime.now(timezone.utc).isoformat(),
        "engine": "Unreal Engine 5.8",
        "character": "MHC_CaptainGodfrey (Captain Godfrey)",
        "body_asset": BODY_PATH,
        "face_asset": FACE_PATH if face else None,
        "physics_asset": PHYS_PATH,
        "skeleton": skeleton_name(body),
        "expected_clothing_skeleton": "metahuman_base_skel (MetaHuman clothing workflow)",
        "body_bone_count": bone_count(body),
        "units": "centimeters (Unreal)",
        "body_bounds": body_bounds,
        "body_type_notes": [
            "Custom MetaHuman Creator assemble (not a stock Bridge body).",
            "Target MetaHuman clothing workflow / metahuman_base_skel.",
            "Outfit pieces needed: jacket, trousers, shirt/tie, cap. No body/face/hair. No telescope.",
            "Character is a 19th-century merchant (cargo/passenger) ship captain, not navy.",
        ],
    }
    if face:
        payload["face_bounds"] = mesh_bounds(face)
        payload["face_skeleton"] = skeleton_name(face)

    with open(out_json, "w", encoding="utf-8") as handle:
        json.dump(payload, handle, indent=2)

    height = body_bounds["height_cm"]
    width = body_bounds["width_cm"]
    depth = body_bounds["depth_cm"]
    lines = [
        "Captain Godfrey — MetaHuman body reference for costume fitting",
        "===============================================================",
        f"Exported (UTC): {payload['exported_utc']}",
        "Engine: Unreal Engine 5.8",
        "",
        f"Body mesh asset: {BODY_PATH}",
        f"Skeleton on mesh: {payload['skeleton']}",
        f"Clothing target skeleton: metahuman_base_skel",
        f"Approx mesh bounds height: {height:.1f} cm",
        f"Approx mesh bounds width (X): {width:.1f} cm",
        f"Approx mesh bounds depth (Y): {depth:.1f} cm",
        "",
        "Notes:",
        "- Bounds height is the mesh AABB, not a formal tailor measurement.",
        "- Fit clothing to the BODY FBX proportions and MetaHuman skin weights.",
        "- Deliver clothing only (FBX + textures, or Unreal assets), skinned for MetaHuman.",
        "- Include: jacket, trousers, shirt/tie, cap. Exclude telescope, body, face, hair.",
        "",
        "Files in this folder:",
        "- SKM_MHC_CaptainGodfrey_BodyMesh.fbx  (primary fitting mesh)",
        "- SKM_MHC_CaptainGodfrey_FaceMesh.fbx  (collar/neck reference; optional for fit)",
        "- measurements.json / MEASUREMENTS.txt",
    ]
    with open(out_txt, "w", encoding="utf-8") as handle:
        handle.write("\n".join(lines) + "\n")
    log(f"Wrote measurements: height_cm={height:.2f}")


def main() -> None:
    out_dir = delivery_dir()
    log(f"Delivery dir: {out_dir}")

    body = unreal.load_asset(BODY_PATH)
    if not body:
        raise RuntimeError(f"Missing body mesh: {BODY_PATH}")

    face = unreal.load_asset(FACE_PATH)
    if not face:
        log(f"WARNING: face mesh missing: {FACE_PATH}")

    body_fbx = os.path.join(out_dir, "SKM_MHC_CaptainGodfrey_BodyMesh.fbx")
    face_fbx = os.path.join(out_dir, "SKM_MHC_CaptainGodfrey_FaceMesh.fbx")
    meas_json = os.path.join(out_dir, "measurements.json")
    meas_txt = os.path.join(out_dir, "MEASUREMENTS.txt")

    ok_body = export_fbx(body, body_fbx)
    ok_face = True
    if face:
        ok_face = export_fbx(face, face_fbx)

    write_measurements(body, face, meas_json, meas_txt)

    ok = ok_body and ok_face
    write_report(ok)
    if not ok:
        raise RuntimeError("Export failed — see Saved/ExportGodfreyBodyForCostume.txt")


if __name__ == "__main__":
    main()
