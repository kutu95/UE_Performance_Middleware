"""Freeze the pre-RealityErrol Godfrey performer so we can switch back later.

At RealityErrol migrate time the live shell was still CaptainGodfrey meshes +
Hair_S_Clean / Beard_L_Messy / Mustache_L_Handlebar + MHC_CaptainGodfrey_Outfits1
+ ACE. That snapshot is:

  /Game/MetaHumans/Godfrey/BP_Godfrey_Performer_MHC_Errol_Archive

This script validates it and duplicates a second frozen copy:

  /Game/MetaHumans/Godfrey/BP_Godfrey_Performer_PreRealityErrol_Archive

Does NOT modify live BP_Godfrey_Performer.

Headless:
  UnrealEditor-Cmd.exe ".../UnrealPerformer.uproject"
    -ExecutePythonScript=".../Scripts/freeze_pre_reality_errol_godfrey.py"
    -unattended -nop4 -nosplash -stdout

To put that Godfrey back on stage later:
  Scripts/restore_godfrey_performer_from_pre_reality_errol.py
  then editor-only Scripts/swap_godfrey_world_to_mhc_performer.py
"""
from __future__ import annotations

import os
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

import unreal

import importlib
import godfrey_blueprint_wiring as wiring  # noqa: E402

importlib.reload(wiring)

SOURCE_ARCHIVE = "/Game/MetaHumans/Godfrey/BP_Godfrey_Performer_MHC_Errol_Archive"
FROZEN_ARCHIVE = "/Game/MetaHumans/Godfrey/BP_Godfrey_Performer_PreRealityErrol_Archive"
EXPECTED_BODY = "/Game/MetaHumans/MHC_CaptainGodfrey/Body/SKM_MHC_CaptainGodfrey_BodyMesh"
EXPECTED_FACE = "/Game/MetaHumans/MHC_CaptainGodfrey/Face/SKM_MHC_CaptainGodfrey_FaceMesh"
EXPECTED_OUTFIT = "/Game/MetaHumans/MHC_CaptainGodfrey/Clothing/MHC_CaptainGodfrey_Outfits1"
EXPECTED_GROOMS = (
    "Hair_S_Clean",
    "Beard_L_Messy",
    "Mustache_L_Handlebar",
)
BANNED = ("SKM_MH_RealityErrol", "Hair_S_Casual", "Beard_S_Uneven")

REPORT = "FreezePreRealityErrolGodfrey.txt"
_lines: list[str] = []


def log(msg: str) -> None:
    _lines.append(msg)
    unreal.log(f"[FreezePreRealityErrol] {msg}")


def write_report(ok: bool) -> None:
    path = unreal.Paths.convert_relative_path_to_full(
        unreal.Paths.project_saved_dir() + REPORT
    )
    if "MetaHuman_Baseline" not in path.replace("\\", "/"):
        path = r"D:/UE Projects/MetaHuman_Baseline_UE58_Test/Saved/" + REPORT
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(("RESULT: PASS\n" if ok else "RESULT: FAIL\n") + "\n".join(_lines) + "\n")
    log(f"Report: {path}")


def _path(obj) -> str:
    if not obj:
        return "(none)"
    try:
        return obj.get_path_name()
    except Exception:
        return str(obj)


def inventory(bp, prefix: str) -> dict[str, str]:
    found: dict[str, str] = {}
    for label, component, _h, _d in wiring.iter_all_components(bp):
        cls = component.get_class().get_name()
        if "SkeletalMesh" in cls:
            mesh = None
            try:
                mesh = component.get_skeletal_mesh_asset()
            except Exception:
                try:
                    mesh = component.get_editor_property("skeletal_mesh")
                except Exception:
                    mesh = None
            path = _path(mesh)
            found[label] = path
            log(f"{prefix} {label} ({cls}): {path}")
        elif "Groom" in cls:
            groom = None
            try:
                groom = component.get_editor_property("groom_asset")
            except Exception:
                groom = None
            path = _path(groom)
            found[label] = path
            log(f"{prefix} {label} ({cls}): {path}")
    return found


def assert_pre_reality_errol(bp, name: str) -> None:
    body, _ = wiring.find_component(bp, "Body")
    face, _ = wiring.find_component(bp, "Face")
    if not body or not face:
        raise RuntimeError(f"{name} missing Body/Face")
    try:
        body_path = _path(body.get_skeletal_mesh_asset())
    except Exception:
        body_path = _path(body.get_editor_property("skeletal_mesh"))
    try:
        face_path = _path(face.get_skeletal_mesh_asset())
    except Exception:
        face_path = _path(face.get_editor_property("skeletal_mesh"))
    if EXPECTED_BODY not in body_path:
        raise RuntimeError(f"{name} body is not CaptainGodfrey: {body_path}")
    if EXPECTED_FACE not in face_path:
        raise RuntimeError(f"{name} face is not CaptainGodfrey: {face_path}")

    body_anim = wiring._anim_class_name(body)
    face_anim = wiring._anim_class_name(face)
    if "GodfreyBodyAnimInstance" not in body_anim:
        raise RuntimeError(f"{name} Body AnimClass {body_anim}")
    if "Face_AnimBP" not in face_anim:
        raise RuntimeError(f"{name} Face AnimClass {face_anim}")

    if not wiring.find_component_by_class(bp, "ACEAudioCurveSourceComponent")[0]:
        raise RuntimeError(f"{name} missing ACEAudioCurveSource")
    if not wiring.find_component_by_class(bp, "GodfreyPerformanceStateComponent")[0]:
        raise RuntimeError(f"{name} missing PerformanceState")
    if not wiring.find_component_by_class(bp, "GodfreyPerformerAnimationBridgeComponent")[0]:
        raise RuntimeError(f"{name} missing AnimationBridge")

    grooms = inventory(bp, name)
    blob = " ".join(grooms.values())
    for token in BANNED:
        if token in blob:
            raise RuntimeError(f"{name} still references {token}")
    for token in EXPECTED_GROOMS:
        if token not in blob:
            raise RuntimeError(f"{name} missing groom {token}")
    if EXPECTED_OUTFIT not in blob:
        log(f"WARN: {name} inventory did not list {EXPECTED_OUTFIT} (may still be on a clothing slot)")
    log(f"{name} OK — CaptainGodfrey body/face/grooms + ACE stack")


def save_bp(bp, path: str) -> None:
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    try:
        bp.modify()
    except Exception:
        pass
    ok = unreal.EditorAssetLibrary.save_loaded_asset(bp, only_if_is_dirty=False)
    if not ok:
        ok = unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False)
    if not ok:
        raise RuntimeError(f"Could not save {path}")
    log(f"Saved {path}")


def main() -> None:
    log("=== Freeze pre-RealityErrol Godfrey (CaptainGodfrey exhibit shell) ===")
    if not unreal.EditorAssetLibrary.does_asset_exist(SOURCE_ARCHIVE):
        raise RuntimeError(f"Missing {SOURCE_ARCHIVE}")
    for asset in (EXPECTED_BODY, EXPECTED_FACE, EXPECTED_OUTFIT):
        if not unreal.EditorAssetLibrary.does_asset_exist(asset):
            raise RuntimeError(f"Missing source asset {asset}")

    source = unreal.load_asset(SOURCE_ARCHIVE)
    if not source:
        raise RuntimeError(f"Could not load {SOURCE_ARCHIVE}")
    unreal.BlueprintEditorLibrary.compile_blueprint(source)
    assert_pre_reality_errol(source, "MHC_Errol_Archive")
    save_bp(source, SOURCE_ARCHIVE)

    if unreal.EditorAssetLibrary.does_asset_exist(FROZEN_ARCHIVE):
        frozen = unreal.load_asset(FROZEN_ARCHIVE)
        log(f"Frozen copy already exists: {FROZEN_ARCHIVE}")
    else:
        duplicated = unreal.EditorAssetLibrary.duplicate_asset(SOURCE_ARCHIVE, FROZEN_ARCHIVE)
        if not duplicated and not unreal.EditorAssetLibrary.does_asset_exist(FROZEN_ARCHIVE):
            raise RuntimeError(f"Failed to duplicate {SOURCE_ARCHIVE} -> {FROZEN_ARCHIVE}")
        frozen = unreal.load_asset(FROZEN_ARCHIVE)
        log(f"Created frozen copy {FROZEN_ARCHIVE}")

    if not frozen:
        raise RuntimeError(f"Could not load {FROZEN_ARCHIVE}")
    unreal.BlueprintEditorLibrary.compile_blueprint(frozen)
    assert_pre_reality_errol(frozen, "PreRealityErrol_Archive")
    save_bp(frozen, FROZEN_ARCHIVE)

    live = unreal.load_asset(wiring.GODFREY_PERFORMER_BP)
    if live:
        live_body, _ = wiring.find_component(live, "Body")
        live_path = "(none)"
        if live_body:
            try:
                live_path = _path(live_body.get_skeletal_mesh_asset())
            except Exception:
                live_path = "(none)"
        log(f"Live BP_Godfrey_Performer left untouched (Body={live_path})")

    write_report(True)
    log(
        "PASS — pre-RealityErrol Godfrey frozen. "
        f"Rollback source: {FROZEN_ARCHIVE} (also {SOURCE_ARCHIVE}). "
        "Live RealityErrol performer was not changed."
    )


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        unreal.log_error(f"[FreezePreRealityErrol] {exc}")
        write_report(False)
        sys.exit(1)
