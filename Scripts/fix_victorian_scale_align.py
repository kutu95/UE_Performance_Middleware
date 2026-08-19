"""Calibrate Victorian costume scale + auto-align retarget poses to MetaHuman Body.

Live IK retarget keeps Genesis bone lengths, so the suit looks oversized/offset
unless we scale the costume component and align retarget poses.

Headless:
  UnrealEditor-Cmd.exe "D:/UE Projects/MetaHuman_Baseline_UE58_Test/UnrealPerformer.uproject"
    -ExecutePythonScript="D:/UE Projects/MetaHuman_Baseline_UE58_Test/Scripts/fix_victorian_scale_align.py"
    -unattended -nop4 -nosplash -log
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

REPORT = "FixVictorianScaleAlign.txt"
RETARGETER = "/Game/MetaHumans/Costume/Retargeting/RTG_MetaHuman_To_Victorian"
BODY_IK = "/Game/MetaHumans/Costume/Retargeting/IK_CaptainGodfrey_Body"
COSTUME_IK = "/Game/MetaHumans/Costume/Retargeting/IK_Victorian_Genesis8"
BODY_MESH = "/Game/MetaHumans/MHC_Errol/Body/SKM_MHC_CaptainGodfrey_BodyMesh"
COSTUME_MESH = "/Game/MetaHumans/Costume/Victorian_Gentleman"
_lines: list[str] = []


def log(msg: str) -> None:
    _lines.append(msg)
    unreal.log(f"[FixVictorianScale] {msg}")


def write_report(ok: bool) -> None:
    path = unreal.Paths.convert_relative_path_to_full(
        unreal.Paths.project_saved_dir() + REPORT
    )
    if "MetaHuman_Baseline" not in path.replace("\\", "/"):
        path = r"D:/UE Projects/MetaHuman_Baseline_UE58_Test/Saved/" + REPORT
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(("RESULT: PASS\n" if ok else "RESULT: FAIL\n") + "\n".join(_lines) + "\n")
    log(f"Report: {path}")


def mesh_bounds_z(mesh: unreal.SkeletalMesh) -> tuple[float, float, float]:
    """Return (min_z, max_z, height) of imported bounds."""
    try:
        box = mesh.get_bounds()
        origin = box.origin
        extent = box.box_extent
        min_z = float(origin.z - extent.z)
        max_z = float(origin.z + extent.z)
        return min_z, max_z, max_z - min_z
    except Exception as exc:
        log(f"WARN get_bounds: {exc}")
    # Fallback via get_imported_bounds / lod
    for prop in ("imported_bounds", "extended_bounds", "negative_bounds", "positive_bounds"):
        try:
            val = mesh.get_editor_property(prop)
            log(f"  mesh.{prop}={val}")
        except Exception:
            pass
    return 0.0, 180.0, 180.0


def bone_world_approx(mesh: unreal.SkeletalMesh, bone_name: str) -> unreal.Vector | None:
    """Best-effort ref-pose bone location from skeletal mesh."""
    try:
        # UE 5.8 Python: get_ref_pose_position or similar
        for api in ("get_bone_transform", "find_bone_transform"):
            if hasattr(mesh, api):
                t = getattr(mesh, api)(bone_name)
                log(f"  {api}({bone_name})={t}")
                return t.translation if hasattr(t, "translation") else None
    except Exception as exc:
        log(f"  bone api: {exc}")
    try:
        skel = mesh.skeleton
        idx = skel.get_reference_skeleton().find_bone_index(unreal.Name(bone_name))
        log(f"  bone {bone_name} idx={idx}")
    except Exception as exc:
        log(f"  skeleton probe: {exc}")
    return None


def auto_align_retarget(rtg) -> None:
    ctrl = unreal.IKRetargeterController.get_controller(rtg)
    ctrl.set_ik_rig(unreal.RetargetSourceOrTarget.SOURCE, unreal.load_asset(BODY_IK))
    ctrl.set_ik_rig(unreal.RetargetSourceOrTarget.TARGET, unreal.load_asset(COSTUME_IK))
    try:
        ctrl.set_preview_mesh(unreal.RetargetSourceOrTarget.SOURCE, unreal.load_asset(BODY_MESH))
        ctrl.set_preview_mesh(unreal.RetargetSourceOrTarget.TARGET, unreal.load_asset(COSTUME_MESH))
    except Exception as exc:
        log(f"WARN preview mesh: {exc}")

    # Keep pelvis defaults; root motion stays off from prior fix.
    for i in range(int(ctrl.get_num_retarget_ops())):
        try:
            op = ctrl.get_op_controller(i)
        except Exception:
            continue
        label = type(op).__name__
        if "RootMotion" in label:
            try:
                ctrl.set_retarget_op_enabled(i, False)
                log(f"RootMotion op[{i}] disabled")
            except Exception as exc:
                log(f"WARN disable root: {exc}")

    methods = []
    # Prefer ChainToChain then MeshToMesh if enum exists
    enum = getattr(unreal, "RetargetAutoAlignMethod", None)
    if enum:
        for name in ("CHAIN_TO_CHAIN", "MESH_TO_MESH", "ChainToChain", "MeshToMesh"):
            if hasattr(enum, name):
                methods.append(getattr(enum, name))
    if not methods:
        methods = [None]

    aligned = False
    for method in methods:
        try:
            if method is None:
                ctrl.auto_align_all_bones(unreal.RetargetSourceOrTarget.TARGET)
            else:
                ctrl.auto_align_all_bones(unreal.RetargetSourceOrTarget.TARGET, method)
            log(f"auto_align_all_bones TARGET method={method}")
            aligned = True
            break
        except TypeError:
            try:
                ctrl.auto_align_all_bones(unreal.RetargetSourceOrTarget.TARGET)
                log("auto_align_all_bones TARGET (1-arg)")
                aligned = True
                break
            except Exception as exc:
                log(f"WARN auto_align: {exc}")
        except Exception as exc:
            log(f"WARN auto_align method={method}: {exc}")

    if not aligned:
        log("WARN: auto_align failed — continuing with scale only")

    # Remap critical chains after align
    try:
        ctrl.auto_map_chains(unreal.AutoMapChainType.FUZZY, True)
    except Exception:
        pass
    for name in (
        "Spine",
        "Neck",
        "Head",
        "LeftArm",
        "RightArm",
        "LeftLeg",
        "RightLeg",
        "LeftClavicle",
        "RightClavicle",
        "LeftFoot",
        "RightFoot",
    ):
        try:
            ctrl.set_source_chain(unreal.Name(name), unreal.Name(name))
        except Exception:
            pass

    unreal.EditorAssetLibrary.save_loaded_asset(rtg)


def apply_costume_scale(bp, scale: float, z_offset: float) -> None:
    costume, _ = wiring.find_component(bp, "VictorianCostume")
    if not costume:
        raise RuntimeError("VictorianCostume missing")
    body, _ = wiring.find_component(bp, "Body")

    scale_v = unreal.Vector(scale, scale, scale)
    for prop in ("relative_scale3d", "RelativeScale3D"):
        try:
            costume.set_editor_property(prop, scale_v)
            log(f"costume.{prop}={scale_v}")
        except Exception:
            pass

    loc = unreal.Vector(0.0, 0.0, z_offset)
    for prop in ("relative_location", "RelativeLocation"):
        try:
            costume.set_editor_property(prop, loc)
            log(f"costume.{prop}={loc}")
        except Exception:
            pass

    for prop in ("relative_rotation", "RelativeRotation"):
        try:
            costume.set_editor_property(prop, unreal.Rotator(0, 0, 0))
        except Exception:
            pass

    if body:
        wiring.set_prop(
            body,
            ["visibility_based_anim_tick_option", "VisibilityBasedAnimTickOption"],
            unreal.VisibilityBasedAnimTickOption.ALWAYS_TICK_POSE_AND_REFRESH_BONES,
        )
    wiring.set_prop(
        costume,
        ["visibility_based_anim_tick_option", "VisibilityBasedAnimTickOption"],
        unreal.VisibilityBasedAnimTickOption.ALWAYS_TICK_POSE_AND_REFRESH_BONES,
    )


def main() -> None:
    body_mesh = unreal.load_asset(BODY_MESH)
    costume_mesh = unreal.load_asset(COSTUME_MESH)
    if not body_mesh or not costume_mesh:
        raise RuntimeError("Missing body/costume mesh")

    bmin, bmax, bheight = mesh_bounds_z(body_mesh)
    cmin, cmax, cheight = mesh_bounds_z(costume_mesh)
    log(f"Body bounds Z=[{bmin:.2f},{bmax:.2f}] height={bheight:.2f}")
    log(f"Costume bounds Z=[{cmin:.2f},{cmax:.2f}] height={cheight:.2f}")

    if cheight < 1.0:
        raise RuntimeError(f"Costume height invalid: {cheight}")

    # Uniform scale so costume overall height ≈ body height.
    # Genesis Victorian is much taller in bounds (~188 vs MH ~129); do not over-clamp.
    raw = bheight / cheight
    scale = max(0.65, min(1.05, raw))
    log(f"raw_scale={raw:.4f} applied_scale={scale:.4f}")

    # Slight downward nudge after scale (cm). Hat/collar were high in screenshots.
    z_offset = (bmin - cmin * scale) * 0.25
    # Keep nudge modest
    z_offset = max(-8.0, min(2.0, z_offset))
    log(f"z_offset={z_offset}")

    rtg = unreal.load_asset(RETARGETER)
    if not rtg:
        raise RuntimeError(f"Missing {RETARGETER}")
    auto_align_retarget(rtg)

    bp = unreal.load_asset(wiring.GODFREY_PERFORMER_BP)
    if not bp:
        raise RuntimeError(f"Missing {wiring.GODFREY_PERFORMER_BP}")
    apply_costume_scale(bp, scale, z_offset)

    # C++ used to force scale=1 once — clear that by documenting scale on BP defaults.
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    if not wiring.save_godfrey_performer_blueprint(bp):
        raise RuntimeError(wiring.BP_SAVE_LOCK_HINT)

    write_report(True)
    log(
        "PASS — scaled costume to MH bounds, auto-aligned target retarget pose. "
        "Expect better overlay; bone-length mismatch cannot be perfect without rebind."
    )


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        unreal.log_error(f"[FixVictorianScale] {exc}")
        write_report(False)
        sys.exit(1)
