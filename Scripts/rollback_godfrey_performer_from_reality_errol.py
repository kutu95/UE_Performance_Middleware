"""Roll BP_Godfrey_Performer and the placed Godfrey_World actor back to
pre-RealityErrol CaptainGodfrey (Hair_S_Clean, Beard_L_Messy, handlebar,
MHC_CaptainGodfrey_Outfits1, ACE stack).

Editor (complete rollback — Blueprint + World Partition actor):
  Stop PIE, open Godfrey_World, then
  Tools → Execute Python Script → this file
  Ctrl+S the level

Headless (Blueprint only — do not spawn/destroy WP actors):
  UnrealEditor-Cmd.exe ".../UnrealPerformer.uproject"
    -ExecutePythonScript=".../Scripts/rollback_godfrey_performer_from_reality_errol.py"
    -unattended -nop4 -nosplash -stdout
  Then re-run this file in the open editor to replace the placed actor.
"""
from __future__ import annotations

import os
import sys
import time

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

import importlib

import unreal

import freeze_pre_reality_errol_godfrey as freeze  # noqa: E402
import godfrey_blueprint_wiring as wiring  # noqa: E402
import godfrey_exhibit_guard  # noqa: E402
import restore_godfrey_performer_from_pre_reality_errol as restore  # noqa: E402
import swap_godfrey_world_to_mhc_performer as swap  # noqa: E402

importlib.reload(wiring)
importlib.reload(freeze)
importlib.reload(restore)
importlib.reload(swap)
importlib.reload(godfrey_exhibit_guard)

REPORT = "RollbackGodfreyFromRealityErrol.txt"
LEVEL_PATH = "/Game/Godfrey_World"
PERFORMER_LABEL = "BP_Godfrey_Performer"
EXPECTED_BODY = freeze.EXPECTED_BODY
EXPECTED_FACE = freeze.EXPECTED_FACE
EXPECTED_OUTFIT = freeze.EXPECTED_OUTFIT
EXPECTED_GROOMS = freeze.EXPECTED_GROOMS
BANNED = freeze.BANNED
_lines: list[str] = []


def log(msg: str) -> None:
    _lines.append(msg)
    unreal.log(f"[RollbackRealityErrol] {msg}")


def write_report(ok: bool) -> None:
    path = unreal.Paths.convert_relative_path_to_full(
        unreal.Paths.project_saved_dir() + REPORT
    )
    if "MetaHuman_Baseline" not in path.replace("\\", "/"):
        path = r"D:/UE Projects/MetaHuman_Baseline_UE58_Test/Saved/" + REPORT
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(("RESULT: PASS\n" if ok else "RESULT: FAIL\n") + "\n".join(_lines) + "\n")
    log(f"Report: {path}")


def _is_headless() -> bool:
    cmd = unreal.SystemLibrary.get_command_line().lower()
    return "-unattended" in cmd or "unrealeditor-cmd" in cmd


def _path(obj) -> str:
    if not obj:
        return "(none)"
    try:
        return obj.get_path_name()
    except Exception:
        return str(obj)


def _component_mesh_path(component) -> str:
    try:
        mesh = component.get_skeletal_mesh_asset()
    except Exception:
        try:
            mesh = component.get_editor_property("skeletal_mesh")
        except Exception:
            mesh = None
    return _path(mesh)


def live_is_captain_godfrey() -> bool:
    if not unreal.EditorAssetLibrary.does_asset_exist(wiring.GODFREY_PERFORMER_BP):
        return False
    bp = unreal.load_asset(wiring.GODFREY_PERFORMER_BP)
    if not bp:
        return False
    body, _ = wiring.find_component(bp, "Body")
    if not body:
        return False
    return EXPECTED_BODY in _component_mesh_path(body)


def stop_pie() -> None:
    try:
        world = unreal.EditorLevelLibrary.get_editor_world()
        if world and unreal.SystemLibrary.is_packaged_for_distribution():
            return
    except Exception:
        pass
    try:
        les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
        if les and hasattr(les, "editor_request_end_play"):
            les.editor_request_end_play()
            log("Requested End Play")
    except Exception as exc:
        log(f"End Play: {exc}")


def ensure_godfrey_world() -> None:
    world = unreal.EditorLevelLibrary.get_editor_world()
    world_path = ""
    try:
        world_path = world.get_path_name() if world else ""
    except Exception:
        world_path = ""
    if "Godfrey_World" in world_path:
        log(f"Editor world already {world_path}")
        return
    log(f"Loading {LEVEL_PATH} (was {world_path or '(none)'})")
    loaded = unreal.EditorLoadingAndSavingUtils.load_map(LEVEL_PATH)
    if not loaded:
        raise RuntimeError(f"Could not load {LEVEL_PATH}")


def assert_captain_godfrey_actor(actor) -> None:
    summary = swap._groom_summary(actor)
    log(f"Placed grooms: {summary}")
    blob = summary
    meshes = []
    try:
        comps = list(actor.get_components_by_class(unreal.SkeletalMeshComponent) or [])
    except Exception:
        comps = []
    for comp in comps:
        path = _component_mesh_path(comp)
        meshes.append(f"{comp.get_name()}={path}")
        blob += " " + path
    log("Placed meshes: " + "; ".join(meshes) if meshes else "Placed meshes: (none)")
    for token in BANNED:
        if token in blob:
            raise RuntimeError(f"Placed actor still references {token}: {blob}")
    for token in EXPECTED_GROOMS:
        if token not in blob:
            raise RuntimeError(f"Placed actor missing groom {token}: {summary}")
    if EXPECTED_BODY not in blob:
        raise RuntimeError(f"Placed actor body is not CaptainGodfrey: {blob}")
    if EXPECTED_FACE not in blob:
        raise RuntimeError(f"Placed actor face is not CaptainGodfrey: {blob}")
    if EXPECTED_OUTFIT not in blob:
        log(f"WARN: placed actor did not list {EXPECTED_OUTFIT}")


def wait_for_asset_registry() -> None:
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    if hasattr(registry, "search_all_assets"):
        registry.search_all_assets(True)
    for _ in range(120):
        try:
            if not registry.is_loading_assets():
                break
        except Exception:
            break
        time.sleep(0.25)
    log("Asset registry ready")


def restore_blueprint() -> None:
    if live_is_captain_godfrey():
        log("Live BP_Godfrey_Performer is already CaptainGodfrey — skip Blueprint copy")
        bp = unreal.load_asset(wiring.GODFREY_PERFORMER_BP)
        freeze.assert_pre_reality_errol(bp, "LiveAlreadyRestored")
        return
    restore.main()
    if not live_is_captain_godfrey():
        raise RuntimeError("Restore finished but live Body is not SKM_MHC_CaptainGodfrey_BodyMesh")


def wait_for_performer() -> None:
    # Do not time.sleep on the game thread — that blocks World Partition cell load.
    ensure_godfrey_world()
    world = unreal.EditorLevelLibrary.get_editor_world()
    if world:
        for cmd in ("wp.Editor.LoadAllCells", "wp.Editor.LoadRegion"):
            unreal.SystemLibrary.execute_console_command(world, cmd)
            log(f"Console: {cmd}")
    godfrey_exhibit_guard.require_loaded((PERFORMER_LABEL,))
    log("Godfrey_World performer cell is loaded")


def replace_placed_actor() -> None:
    godfrey_exhibit_guard.reject_headless_spawn()
    stop_pie()
    wait_for_performer()

    bp = unreal.load_asset(wiring.GODFREY_PERFORMER_BP)
    if not bp:
        raise RuntimeError(f"Missing {wiring.GODFREY_PERFORMER_BP}")
    freeze.assert_pre_reality_errol(bp, "LiveBeforeSwap")
    actor = swap.replace_with_fresh_instance(bp)
    assert_captain_godfrey_actor(actor)
    swap.save_level()
    log("Placed Godfrey_World actor is a fresh CaptainGodfrey instance")


def main() -> None:
    log("=== Roll back Godfrey performer from RealityErrol ===")
    wait_for_asset_registry()
    restore_blueprint()

    if _is_headless():
        log(
            "Headless: Blueprint restored only. Open the editor, load Godfrey_World, "
            "and re-run this script to replace the placed actor."
        )
        write_report(True)
        return

    replace_placed_actor()
    write_report(True)
    log(
        "PASS — BP_Godfrey_Performer and the placed Godfrey_World actor are the "
        "pre-RealityErrol CaptainGodfrey shell (coat, Hair_S_Clean, Beard_L_Messy, ACE). "
        "MH_RealityErrol is unchanged. Ctrl+S if prompted, then PIE."
    )


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        unreal.log_error(f"[RollbackRealityErrol] {exc}")
        write_report(False)
        sys.exit(1)
