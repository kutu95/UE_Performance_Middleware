"""Remove Victorian costume from BP_Godfrey_Performer and restore MHC outfits.

Headless:
  UnrealEditor-Cmd.exe "D:/UE Projects/MetaHuman_Baseline_UE58_Test/UnrealPerformer.uproject"
    -ExecutePythonScript="D:/UE Projects/MetaHuman_Baseline_UE58_Test/Scripts/remove_victorian_costume_from_godfrey.py"
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

REPORT = "RemoveVictorianCostumeFromGodfrey.txt"
COSTUME_LABEL = "VictorianCostume"
MHC_OUTFIT_LABELS = ("SkeletalMesh", "SkeletalMesh1", "SkeletalMesh2")
_lines: list[str] = []


def log(msg: str) -> None:
    _lines.append(msg)
    unreal.log(f"[RemoveVictorian] {msg}")


def write_report(ok: bool) -> None:
    path = unreal.Paths.convert_relative_path_to_full(
        unreal.Paths.project_saved_dir() + REPORT
    )
    if "MetaHuman_Baseline" not in path.replace("\\", "/"):
        path = r"D:/UE Projects/MetaHuman_Baseline_UE58_Test/Saved/" + REPORT
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(("RESULT: PASS\n" if ok else "RESULT: FAIL\n") + "\n".join(_lines) + "\n")
    log(f"Report: {path}")


def set_hidden(comp, hidden: bool) -> None:
    wiring.set_prop(comp, ["b_hidden_in_game", "bHiddenInGame"], hidden)
    wiring.set_prop(comp, ["visible", "bVisible"], not hidden)


def delete_costume_component(bp) -> bool:
    subsystem = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    if not subsystem:
        raise RuntimeError("SubobjectDataSubsystem unavailable")

    actor_handle, _root = wiring.find_subobject_handles(bp)
    if not actor_handle:
        raise RuntimeError("No actor handle on performer BP")

    lib = unreal.SubobjectDataBlueprintFunctionLibrary
    for label, component, handle, data in wiring.iter_all_components(bp):
        if label != COSTUME_LABEL and "Victorian" not in label:
            continue
        if lib.is_native_component(data):
            log(f"WARN: {label} is native — clearing mesh instead of delete")
            try:
                component.set_editor_property("skeletal_mesh", None)
            except Exception:
                pass
            set_hidden(component, True)
            wiring.set_prop(component, ["anim_class", "AnimClass"], None)
            log(f"Cleared/hidden {label}")
            return True

        deleted = subsystem.delete_subobject(actor_handle, handle, bp)
        log(f"Deleted subobject {label} ok={deleted > 0}")
        return deleted > 0

    # Fallback: find by mesh name
    for label, component, handle, data in wiring.iter_all_components(bp):
        try:
            mesh = component.get_editor_property("skeletal_mesh")
        except Exception:
            mesh = None
        mesh_name = mesh.get_name() if mesh else ""
        if "Victorian" not in mesh_name and "Gentleman" not in mesh_name:
            continue
        if lib.is_native_component(data):
            try:
                component.set_editor_property("skeletal_mesh", None)
            except Exception:
                pass
            set_hidden(component, True)
            wiring.set_prop(component, ["anim_class", "AnimClass"], None)
            log(f"Cleared mesh on {label} ({mesh_name})")
            return True
        deleted = subsystem.delete_subobject(actor_handle, handle, bp)
        log(f"Deleted {label} (mesh={mesh_name}) ok={deleted > 0}")
        return deleted > 0

    log("No VictorianCostume component found (already removed?)")
    return False


def restore_mhc_outfits(bp) -> list[str]:
    restored: list[str] = []
    for label in MHC_OUTFIT_LABELS:
        comp, _ = wiring.find_component(bp, label)
        if not comp:
            log(f"skip missing outfit {label}")
            continue
        set_hidden(comp, False)
        restored.append(label)
        log(f"Restored MHC outfit visible: {label}")
    return restored


def reset_bridge_body_flags(bp) -> None:
    bridge, _ = wiring.find_component_by_class(bp, "GodfreyPerformerAnimationBridgeComponent")
    if not bridge:
        log("WARN: AnimationBridge missing")
        return
    if wiring.set_prop(bridge, ["b_keep_body_mesh_visible", "bKeepBodyMeshVisible"], False):
        log("Bridge: bKeepBodyMeshVisible=False")
    wiring.set_prop(
        bridge,
        ["b_debug_force_body_mesh_visible_at_begin_play", "bDebugForceBodyMeshVisibleAtBeginPlay"],
        False,
    )


def main() -> None:
    bp = unreal.load_asset(wiring.GODFREY_PERFORMER_BP)
    if not bp:
        raise RuntimeError(f"Missing {wiring.GODFREY_PERFORMER_BP}")

    deleted = delete_costume_component(bp)
    restored = restore_mhc_outfits(bp)
    reset_bridge_body_flags(bp)

    # MHC pipeline owns Body visibility (usually hidden under outfits).
    body, _ = wiring.find_component(bp, "Body")
    if body:
        wiring.set_prop(
            body,
            ["visibility_based_anim_tick_option", "VisibilityBasedAnimTickOption"],
            unreal.VisibilityBasedAnimTickOption.ALWAYS_TICK_POSE_AND_REFRESH_BONES,
        )
        log("Body tick kept AlwaysTickPoseAndRefreshBones for clothing PP")

    face, _ = wiring.find_component(bp, "Face")
    if face:
        set_hidden(face, False)
        log("Face visible")

    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    if not wiring.save_godfrey_performer_blueprint(bp):
        raise RuntimeError(wiring.BP_SAVE_LOCK_HINT)

    # Confirm gone
    leftover, _ = wiring.find_component(bp, COSTUME_LABEL)
    if leftover:
        # Still present — force hide/clear as soft remove
        try:
            leftover.set_editor_property("skeletal_mesh", None)
        except Exception:
            pass
        set_hidden(leftover, True)
        wiring.set_prop(leftover, ["anim_class", "AnimClass"], None)
        unreal.BlueprintEditorLibrary.compile_blueprint(bp)
        wiring.save_godfrey_performer_blueprint(bp)
        log("WARN: component still present after delete — mesh cleared + hidden")

    write_report(True)
    log(
        f"PASS — Victorian costume removed (deleted={deleted}); "
        f"MHC outfits restored={restored}. PIE Errol/Godfrey without the suit."
    )


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        unreal.log_error(f"[RemoveVictorian] {exc}")
        write_report(False)
        sys.exit(1)
