"""Put the pre-RealityErrol Godfrey back onto BP_Godfrey_Performer.

Archives the current live RealityErrol shell first, then copies
BP_Godfrey_Performer_PreRealityErrol_Archive (CaptainGodfrey + ACE) onto
the live performer path.

Does NOT spawn/destroy World Partition actors. After PASS, open Godfrey_World
and run Scripts/rollback_godfrey_performer_from_reality_errol.py (or the
editor-only swap at the end of that script) so the placed actor is a fresh
CaptainGodfrey instance. Do not run swap_godfrey_world_to_mhc_performer.py
after this restore — that script still expects RealityErrol.

Headless (editor closed):
  UnrealEditor-Cmd.exe ".../UnrealPerformer.uproject"
    -ExecutePythonScript=".../Scripts/restore_godfrey_performer_from_pre_reality_errol.py"
    -unattended -nop4 -nosplash -stdout
"""
from __future__ import annotations

import os
import sys
import time

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

import unreal

import importlib
import freeze_pre_reality_errol_godfrey as freeze  # noqa: E402
import godfrey_blueprint_wiring as wiring  # noqa: E402

importlib.reload(wiring)
importlib.reload(freeze)

LIVE_BP = wiring.GODFREY_PERFORMER_BP
FROZEN_ARCHIVE = "/Game/MetaHumans/Godfrey/BP_Godfrey_Performer_PreRealityErrol_Archive"
SOURCE_ARCHIVE = "/Game/MetaHumans/Godfrey/BP_Godfrey_Performer_MHC_Errol_Archive"
FORWARD_ARCHIVE = "/Game/MetaHumans/Godfrey/BP_Godfrey_Performer_RealityErrol_Archive"

REPORT = "RestorePreRealityErrolGodfrey.txt"
_lines: list[str] = []


def log(msg: str) -> None:
    _lines.append(msg)
    unreal.log(f"[RestorePreRealityErrol] {msg}")


def write_report(ok: bool) -> None:
    path = unreal.Paths.convert_relative_path_to_full(
        unreal.Paths.project_saved_dir() + REPORT
    )
    if "MetaHuman_Baseline" not in path.replace("\\", "/"):
        path = r"D:/UE Projects/MetaHuman_Baseline_UE58_Test/Saved/" + REPORT
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(("RESULT: PASS\n" if ok else "RESULT: FAIL\n") + "\n".join(_lines) + "\n")
    log(f"Report: {path}")


def _close_asset_editors(asset_path: str) -> None:
    try:
        asset = unreal.load_asset(asset_path)
        if not asset:
            return
        subsystem = unreal.get_editor_subsystem(unreal.AssetEditorSubsystem)
        if subsystem:
            subsystem.close_all_editors_for_asset(asset)
    except Exception:
        pass


def _force_delete_asset(asset_path: str) -> None:
    if not unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        return
    _close_asset_editors(asset_path)
    unreal.EditorAssetLibrary.delete_asset(asset_path)
    if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        time.sleep(1.0)
        _close_asset_editors(asset_path)
        unreal.EditorAssetLibrary.delete_asset(asset_path)
    if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        raise RuntimeError(f"Could not delete {asset_path} (file locked?)")
    log(f"Deleted {asset_path}")


def resolve_source() -> str:
    if unreal.EditorAssetLibrary.does_asset_exist(FROZEN_ARCHIVE):
        return FROZEN_ARCHIVE
    if unreal.EditorAssetLibrary.does_asset_exist(SOURCE_ARCHIVE):
        log(f"WARN: using {SOURCE_ARCHIVE} (run freeze_pre_reality_errol_godfrey.py for a second copy)")
        return SOURCE_ARCHIVE
    raise RuntimeError(
        f"Missing rollback archives {FROZEN_ARCHIVE} and {SOURCE_ARCHIVE}"
    )


def main() -> None:
    log("=== Restore pre-RealityErrol Godfrey onto BP_Godfrey_Performer ===")
    source = resolve_source()
    log(f"Restore from {source}")

    if unreal.EditorAssetLibrary.does_asset_exist(LIVE_BP):
        if not unreal.EditorAssetLibrary.does_asset_exist(FORWARD_ARCHIVE):
            duplicated = unreal.EditorAssetLibrary.duplicate_asset(LIVE_BP, FORWARD_ARCHIVE)
            if not duplicated and not unreal.EditorAssetLibrary.does_asset_exist(FORWARD_ARCHIVE):
                raise RuntimeError(f"Failed to archive live performer -> {FORWARD_ARCHIVE}")
            log(f"Archived current live shell -> {FORWARD_ARCHIVE}")
            unreal.EditorAssetLibrary.save_asset(FORWARD_ARCHIVE, only_if_is_dirty=False)
        else:
            log(f"Forward archive already exists: {FORWARD_ARCHIVE}")
        _force_delete_asset(LIVE_BP)

    duplicated = unreal.EditorAssetLibrary.duplicate_asset(source, LIVE_BP)
    if not duplicated and not unreal.EditorAssetLibrary.does_asset_exist(LIVE_BP):
        raise RuntimeError(f"Failed to copy {source} -> {LIVE_BP}")

    bp = unreal.load_asset(LIVE_BP)
    if not bp:
        raise RuntimeError(f"Could not load restored {LIVE_BP}")
    freeze.assert_pre_reality_errol(bp, "RestoredLive")
    if not wiring.save_godfrey_performer_blueprint(bp):
        raise RuntimeError(wiring.BP_SAVE_LOCK_HINT)

    write_report(True)
    log(
        "PASS — BP_Godfrey_Performer is the pre-RealityErrol CaptainGodfrey shell. "
        "RealityErrol kept at "
        f"{FORWARD_ARCHIVE}. Next: in the open editor run "
        "Scripts/rollback_godfrey_performer_from_reality_errol.py so the placed "
        "Godfrey_World actor is a fresh CaptainGodfrey instance."
    )


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        unreal.log_error(f"[RestorePreRealityErrol] {exc}")
        write_report(False)
        sys.exit(1)
