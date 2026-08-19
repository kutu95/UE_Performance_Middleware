"""Ensure Victorian RTG has default ops + mapped chains (UE 5.8).

Without AddDefaultOps(), AutoMapChains/SetSourceChain are no-ops and the
costume stays in A-pose at runtime.

Headless:
  UnrealEditor-Cmd.exe "D:/UE Projects/MetaHuman_Baseline_UE58_Test/UnrealPerformer.uproject"
    -ExecutePythonScript="D:/UE Projects/MetaHuman_Baseline_UE58_Test/Scripts/fix_victorian_chain_maps.py"
    -unattended -nop4 -nosplash -log
"""
from __future__ import annotations

import os
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

import unreal

REPORT = "FixVictorianChainMaps.txt"
RETARGETER = "/Game/MetaHumans/Costume/Retargeting/RTG_MetaHuman_To_Victorian"
BODY_IK = "/Game/MetaHumans/Costume/Retargeting/IK_CaptainGodfrey_Body"
COSTUME_IK = "/Game/MetaHumans/Costume/Retargeting/IK_Victorian_Genesis8"
BODY_MESH = "/Game/MetaHumans/MHC_Errol/Body/SKM_MHC_CaptainGodfrey_BodyMesh"
COSTUME_MESH = "/Game/MetaHumans/Costume/Victorian_Gentleman"
_lines: list[str] = []


def log(msg: str) -> None:
    _lines.append(msg)
    unreal.log(f"[FixVictorianMaps] {msg}")


def write_report(ok: bool) -> None:
    path = unreal.Paths.convert_relative_path_to_full(
        unreal.Paths.project_saved_dir() + REPORT
    )
    if "MetaHuman_Baseline" not in path.replace("\\", "/"):
        path = r"D:/UE Projects/MetaHuman_Baseline_UE58_Test/Saved/" + REPORT
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(("RESULT: PASS\n" if ok else "RESULT: FAIL\n") + "\n".join(_lines) + "\n")
    log(f"Report: {path}")


def chain_name(chain) -> str | None:
    try:
        return str(chain.get_editor_property("chain_name"))
    except Exception:
        try:
            return str(chain.chain_name)
        except Exception:
            return None


def main() -> None:
    rtg = unreal.load_asset(RETARGETER)
    body_ik = unreal.load_asset(BODY_IK)
    costume_ik = unreal.load_asset(COSTUME_IK)
    body_mesh = unreal.load_asset(BODY_MESH)
    costume_mesh = unreal.load_asset(COSTUME_MESH)
    if not all((rtg, body_ik, costume_ik, body_mesh, costume_mesh)):
        raise RuntimeError("Missing retarget assets")

    ctrl = unreal.IKRetargeterController.get_controller(rtg)

    num_ops = int(ctrl.get_num_retarget_ops())
    log(f"Retarget ops before: {num_ops}")
    if num_ops <= 0:
        ctrl.add_default_ops()
        log(f"add_default_ops -> ops={ctrl.get_num_retarget_ops()}")

    ctrl.set_ik_rig(unreal.RetargetSourceOrTarget.SOURCE, body_ik)
    ctrl.set_ik_rig(unreal.RetargetSourceOrTarget.TARGET, costume_ik)
    try:
        ctrl.set_preview_mesh(unreal.RetargetSourceOrTarget.SOURCE, body_mesh)
        ctrl.set_preview_mesh(unreal.RetargetSourceOrTarget.TARGET, costume_mesh)
    except Exception as exc:
        log(f"WARN set_preview_mesh: {exc}")

    try:
        ctrl.clean_chain_maps()
        log("clean_chain_maps OK")
    except Exception as exc:
        log(f"WARN clean_chain_maps: {exc}")

    try:
        ctrl.auto_map_chains(unreal.AutoMapChainType.FUZZY, True)
        log("auto_map_chains FUZZY")
    except Exception as exc:
        log(f"WARN auto_map: {exc}")

    body_names = {
        chain_name(c)
        for c in unreal.IKRigController.get_controller(body_ik).get_retarget_chains()
    }
    body_names.discard(None)

    costume_chains = list(
        unreal.IKRigController.get_controller(costume_ik).get_retarget_chains()
    )
    mapped = 0
    for chain in costume_chains:
        name = chain_name(chain)
        if not name or name not in body_names:
            continue
        try:
            ok = bool(ctrl.set_source_chain(unreal.Name(name), unreal.Name(name)))
            got = str(ctrl.get_source_chain(unreal.Name(name)))
            log(f"  {name} <= {got} ok={ok}")
            if got and got != "None":
                mapped += 1
        except Exception as exc:
            log(f"  FAIL {name}: {exc}")

    unreal.EditorAssetLibrary.save_loaded_asset(rtg)
    log(f"Mapped {mapped} shared chains; ops={ctrl.get_num_retarget_ops()}")
    if mapped < 6:
        raise RuntimeError(f"Too few chain maps ({mapped})")
    write_report(True)


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        unreal.log_error(f"[FixVictorianMaps] {exc}")
        write_report(False)
        sys.exit(1)
