"""Dump RTG chain mappings and force-refresh Body->Victorian retargeter.

Headless:
  UnrealEditor-Cmd.exe "D:/UE Projects/MetaHuman_Baseline_UE58_Test/UnrealPerformer.uproject"
    -ExecutePythonScript="D:/UE Projects/MetaHuman_Baseline_UE58_Test/Scripts/diagnose_victorian_retarget.py"
    -unattended -nop4 -nosplash -log
"""
from __future__ import annotations

import os
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

import unreal

REPORT = "DiagnoseVictorianRetarget.txt"
RETARGETER = "/Game/MetaHumans/Costume/Retargeting/RTG_MetaHuman_To_Victorian"
BODY_IK = "/Game/MetaHumans/Costume/Retargeting/IK_CaptainGodfrey_Body"
COSTUME_IK = "/Game/MetaHumans/Costume/Retargeting/IK_Victorian_Genesis8"
BODY_MESH = "/Game/MetaHumans/MHC_Errol/Body/SKM_MHC_CaptainGodfrey_BodyMesh"
COSTUME_MESH = "/Game/MetaHumans/Costume/Victorian_Gentleman"
_lines: list[str] = []


def log(msg: str) -> None:
    _lines.append(msg)
    unreal.log(f"[DiagVictorianRTG] {msg}")


def write_report(ok: bool) -> None:
    path = unreal.Paths.convert_relative_path_to_full(
        unreal.Paths.project_saved_dir() + REPORT
    )
    if "MetaHuman_Baseline" not in path.replace("\\", "/"):
        path = r"D:/UE Projects/MetaHuman_Baseline_UE58_Test/Saved/" + REPORT
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(("RESULT: PASS\n" if ok else "RESULT: FAIL\n") + "\n".join(_lines) + "\n")
    log(f"Report: {path}")


def dump_ik_chains(label: str, ik_path: str) -> None:
    ik = unreal.load_asset(ik_path)
    if not ik:
        log(f"MISSING IK {ik_path}")
        return
    ctrl = unreal.IKRigController.get_controller(ik)
    mesh = None
    for getter in ("get_skeletal_mesh", "get_preview_mesh"):
        try:
            mesh = getattr(ctrl, getter)()
            break
        except Exception:
            pass
    log(f"{label} ik={ik_path} preview={mesh}")
    try:
        chains = list(ctrl.get_retarget_chains())
    except Exception as exc:
        log(f"{label} get_retarget_chains failed: {exc}")
        return
    log(f"{label} chain_count={len(chains)}")
    for chain in chains:
        bits = []
        for attr in (
            "chain_name",
            "ChainName",
            "start_bone",
            "StartBone",
            "end_bone",
            "EndBone",
            "name",
        ):
            try:
                val = getattr(chain, attr)
                if val is not None:
                    bits.append(f"{attr}={val}")
            except Exception:
                pass
        # struct properties
        for prop in ("chain_name", "start_bone", "end_bone"):
            try:
                bits.append(f"{prop}={chain.get_editor_property(prop)}")
            except Exception:
                pass
        log(f"  chain: {', '.join(bits) if bits else repr(chain)}")


def dump_rtg_maps(rtg) -> None:
    ctrl = unreal.IKRetargeterController.get_controller(rtg)
    # Try several APIs across UE versions
    mapped = []
    for api in ("get_chain_mapping", "get_retarget_chain_mapping", "get_mapped_chains"):
        if hasattr(ctrl, api):
            try:
                mapped = list(getattr(ctrl, api)())
                log(f"Mapped via {api}: count={len(mapped)}")
                break
            except Exception as exc:
                log(f"{api} failed: {exc}")

    # Table of source/target chain names from asset
    for prop in ("chain_settings", "ChainSettings", "retarget_chain_pairs"):
        try:
            val = rtg.get_editor_property(prop)
            log(f"rtg.{prop}={val}")
        except Exception:
            pass

    # Explicit remap using known MetaHuman <-> Genesis names
    pairs = (
        ("Root", "Root"),
        ("Spine", "Spine"),
        ("Head", "Head"),
        ("LeftClavicle", "LeftClavicle"),
        ("RightClavicle", "RightClavicle"),
        ("LeftArm", "LeftArm"),
        ("RightArm", "RightArm"),
        ("LeftLeg", "LeftLeg"),
        ("RightLeg", "RightLeg"),
    )
    for src, tgt in pairs:
        ok = False
        for method in ("set_chain_mapping", "map_chain", "set_retarget_chain_mapping"):
            if not hasattr(ctrl, method):
                continue
            try:
                getattr(ctrl, method)(unreal.Name(src), unreal.Name(tgt))
                ok = True
                log(f"map {src}->{tgt} via {method} OK")
                break
            except TypeError:
                try:
                    getattr(ctrl, method)(src, tgt)
                    ok = True
                    log(f"map {src}->{tgt} via {method} (str) OK")
                    break
                except Exception as exc:
                    log(f"map {src}->{tgt} {method}: {exc}")
            except Exception as exc:
                log(f"map {src}->{tgt} {method}: {exc}")
        if not ok:
            log(f"WARN: could not explicitly map {src}->{tgt}")

    try:
        ctrl.auto_map_chains(unreal.AutoMapChainType.EXACT, True)
        log("auto_map_chains EXACT OK")
    except Exception as exc:
        log(f"auto_map EXACT: {exc}")
    try:
        ctrl.auto_map_chains(unreal.AutoMapChainType.FUZZY, True)
        log("auto_map_chains FUZZY OK")
    except Exception as exc:
        log(f"auto_map FUZZY: {exc}")

    unreal.EditorAssetLibrary.save_loaded_asset(rtg)


def main() -> None:
    dump_ik_chains("BODY", BODY_IK)
    dump_ik_chains("COSTUME", COSTUME_IK)

    rtg = unreal.load_asset(RETARGETER)
    body_ik = unreal.load_asset(BODY_IK)
    costume_ik = unreal.load_asset(COSTUME_IK)
    body_mesh = unreal.load_asset(BODY_MESH)
    costume_mesh = unreal.load_asset(COSTUME_MESH)
    if not all((rtg, body_ik, costume_ik, body_mesh, costume_mesh)):
        raise RuntimeError("Missing retarget assets")

    ctrl = unreal.IKRetargeterController.get_controller(rtg)
    ctrl.set_ik_rig(unreal.RetargetSourceOrTarget.SOURCE, body_ik)
    ctrl.set_ik_rig(unreal.RetargetSourceOrTarget.TARGET, costume_ik)
    try:
        ctrl.set_preview_mesh(unreal.RetargetSourceOrTarget.SOURCE, body_mesh)
        ctrl.set_preview_mesh(unreal.RetargetSourceOrTarget.TARGET, costume_mesh)
    except Exception as exc:
        log(f"set_preview_mesh: {exc}")

    dump_rtg_maps(rtg)

    # List controller methods for debugging
    methods = [m for m in dir(ctrl) if "chain" in m.lower() or "map" in m.lower()]
    log(f"controller map/chain methods: {methods}")

    write_report(True)


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        unreal.log_error(f"[DiagVictorianRTG] {exc}")
        write_report(False)
        sys.exit(1)
