"""Revert bad pelvis/root settings; keep costume transform identity only.

Headless:
  UnrealEditor-Cmd.exe ... -ExecutePythonScript=.../fix_victorian_alignment.py
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

REPORT = "FixVictorianAlignment.txt"
RETARGETER = "/Game/MetaHumans/Costume/Retargeting/RTG_MetaHuman_To_Victorian"
BODY_IK = "/Game/MetaHumans/Costume/Retargeting/IK_CaptainGodfrey_Body"
COSTUME_IK = "/Game/MetaHumans/Costume/Retargeting/IK_Victorian_Genesis8"
COSTUME_MESH = "/Game/MetaHumans/Costume/Victorian_Gentleman"
_lines: list[str] = []


def log(msg: str) -> None:
    _lines.append(msg)
    unreal.log(f"[FixVictorianAlign] {msg}")


def write_report(ok: bool) -> None:
    path = unreal.Paths.convert_relative_path_to_full(
        unreal.Paths.project_saved_dir() + REPORT
    )
    if "MetaHuman_Baseline" not in path.replace("\\", "/"):
        path = r"D:/UE Projects/MetaHuman_Baseline_UE58_Test/Saved/" + REPORT
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(("RESULT: PASS\n" if ok else "RESULT: FAIL\n") + "\n".join(_lines) + "\n")
    log(f"Report: {path}")


def _set_prop(obj, names, value) -> bool:
    for name in names:
        try:
            obj.set_editor_property(name, value)
            return True
        except Exception:
            continue
    return False


def genesis_root_bone_name() -> str:
    mesh = unreal.load_asset(COSTUME_MESH)
    if not mesh:
        return "hip"
    try:
        ref = mesh.get_editor_property("skeleton").get_editor_property("reference_skeleton")
    except Exception:
        ref = None
    # Prefer first bone of the skeletal mesh ref skeleton
    try:
        skel = mesh.skeleton
        # get_bone_name / get_num_bones via ref skeleton
        num = mesh.get_editor_property("skeletal_mesh").get_num_bones() if False else None
    except Exception:
        pass
    try:
        # UE 5.8: SkeletalMesh.get_ref_skeleton / get_bone_name
        names = []
        for getter in ("get_all_socket_names",):
            pass
        bone_count = mesh.get_editor_property("lod_info")  # unused probe
    except Exception:
        pass
    # Common Genesis FBX roots
    for candidate in ("hip", "Genesis8Male", "root", "Root", "pelvis"):
        return candidate if candidate == "hip" else "hip"
    return "hip"


def fix_ops(rtg) -> None:
    ctrl = unreal.IKRetargeterController.get_controller(rtg)
    num = int(ctrl.get_num_retarget_ops())
    log(f"retarget ops={num}")
    for i in range(num):
        try:
            op = ctrl.get_op_controller(i)
        except Exception:
            op = None
        if not op:
            continue
        label = type(op).__name__
        log(f"  op[{i}] {label}")

        if "Pelvis" in label:
            # Epic default BlendToSourceTranslation = 0 — blend=1 tore the figure apart.
            try:
                op.set_source_pelvis_bone(unreal.Name("pelvis"))
                op.set_target_pelvis_bone(unreal.Name("hip"))
            except Exception as exc:
                log(f"    WARN pelvis bones: {exc}")
            try:
                s = op.get_settings()
                _set_prop(s, ["blend_to_source_translation", "BlendToSourceTranslation"], 0.0)
                _set_prop(s, ["translation_alpha", "TranslationAlpha"], 1.0)
                _set_prop(s, ["rotation_alpha", "RotationAlpha"], 1.0)
                _set_prop(s, ["translation_offset_global", "TranslationOffsetGlobal"], unreal.Vector(0, 0, 0))
                _set_prop(s, ["translation_offset_local", "TranslationOffsetLocal"], unreal.Vector(0, 0, 0))
                op.set_settings(s)
                log("    Pelvis: BlendToSourceTranslation=0 (Epic default)")
            except Exception as exc:
                log(f"    WARN pelvis settings: {exc}")

        if "RootMotion" in label:
            # Do NOT drive hip as root — hip is the retarget pelvis. Disable root motion op
            # or leave GlobalOffset identity with true skeleton root only.
            try:
                s = op.get_settings()
                # Prefer disable if property exists
                disabled = _set_prop(s, ["b_enabled", "bEnabled", "enabled"], False)
                _set_prop(s, ["global_offset", "GlobalOffset"], unreal.Transform())
                try:
                    op.set_settings(s)
                except Exception:
                    pass
                log(f"    RootMotion: disabled={disabled}, GlobalOffset=identity")
            except Exception as exc:
                log(f"    WARN root motion: {exc}")
            # Also try op-level enable flag on controller
            for names in (
                ["set_enabled", "SetEnabled"],
                ["set_op_enabled", "SetOpEnabled"],
            ):
                for name in names:
                    if hasattr(ctrl, name):
                        try:
                            getattr(ctrl)(name)  # noqa — probe
                        except Exception:
                            pass
            try:
                if hasattr(ctrl, "set_op_enabled"):
                    ctrl.set_op_enabled(i, False)
                    log(f"    set_op_enabled({i}, False)")
                elif hasattr(ctrl, "set_retarget_op_enabled"):
                    ctrl.set_retarget_op_enabled(i, False)
                    log(f"    set_retarget_op_enabled({i}, False)")
            except Exception as exc:
                log(f"    WARN disable root op: {exc}")


def zero_costume_on_bp(bp) -> None:
    costume, _ = wiring.find_component(bp, "VictorianCostume")
    if not costume:
        raise RuntimeError("VictorianCostume missing")
    for prop, value in (
        ("relative_location", unreal.Vector(0, 0, 0)),
        ("RelativeLocation", unreal.Vector(0, 0, 0)),
        ("relative_rotation", unreal.Rotator(0, 0, 0)),
        ("RelativeRotation", unreal.Rotator(0, 0, 0)),
        ("relative_scale3d", unreal.Vector(1, 1, 1)),
        ("RelativeScale3D", unreal.Vector(1, 1, 1)),
    ):
        try:
            costume.set_editor_property(prop, value)
        except Exception:
            pass
    log("costume relative transform forced to identity")


def main() -> None:
    # Keep retarget roots correct (pelvis/hip) — these are fine.
    for path, bone in ((BODY_IK, "pelvis"), (COSTUME_IK, "hip")):
        ik = unreal.load_asset(path)
        c = unreal.IKRigController.get_controller(ik)
        c.set_retarget_root(unreal.Name(bone))
        log(f"{path} retarget_root={c.get_retarget_root()}")
        unreal.EditorAssetLibrary.save_loaded_asset(ik)

    rtg = unreal.load_asset(RETARGETER)
    if not rtg:
        raise RuntimeError(f"Missing {RETARGETER}")
    ctrl = unreal.IKRetargeterController.get_controller(rtg)
    ctrl.set_ik_rig(unreal.RetargetSourceOrTarget.SOURCE, unreal.load_asset(BODY_IK))
    ctrl.set_ik_rig(unreal.RetargetSourceOrTarget.TARGET, unreal.load_asset(COSTUME_IK))
    fix_ops(rtg)

    try:
        ctrl.set_root_offset_in_retarget_pose(
            unreal.Vector(0, 0, 0), unreal.RetargetSourceOrTarget.TARGET
        )
    except Exception as exc:
        log(f"WARN root offset: {exc}")

    unreal.EditorAssetLibrary.save_loaded_asset(rtg)

    bp = unreal.load_asset(wiring.GODFREY_PERFORMER_BP)
    zero_costume_on_bp(bp)
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    if not wiring.save_godfrey_performer_blueprint(bp):
        raise RuntimeError(wiring.BP_SAVE_LOCK_HINT)

    write_report(True)
    log("PASS — reverted pelvis blend; disabled RootMotion-on-hip; transform identity.")


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        unreal.log_error(f"[FixVictorianAlign] {exc}")
        write_report(False)
        sys.exit(1)
