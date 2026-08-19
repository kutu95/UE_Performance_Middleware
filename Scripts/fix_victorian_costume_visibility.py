"""Keep MetaHuman Body visible (hands/neck) + Victorian costume; hide MHC outfits only.

Enables bridge bKeepBodyMeshVisible so garment/MH paths cannot re-hide Body.

Headless:
  UnrealEditor-Cmd.exe "D:/UE Projects/MetaHuman_Baseline_UE58_Test/UnrealPerformer.uproject"
    -ExecutePythonScript="D:/UE Projects/MetaHuman_Baseline_UE58_Test/Scripts/fix_victorian_costume_visibility.py"
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

REPORT = "FixVictorianCostumeVisibility.txt"
HIDE_LABELS = (
    "SkeletalMesh",
    "SkeletalMesh1",
    "SkeletalMesh2",
)
KEEP_VISIBLE = {
    "Body",
    "Face",
    "VictorianCostume",
    "Hair",
    "Beard",
    "Mustache",
    "Eyebrows",
    "Eyelashes",
    "Fuzz",
}
_lines: list[str] = []


def log(msg: str) -> None:
    _lines.append(msg)
    unreal.log(f"[FixVictorianVis] {msg}")


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


def main() -> None:
    bp = unreal.load_asset(wiring.GODFREY_PERFORMER_BP)
    if not bp:
        raise RuntimeError(f"Missing {wiring.GODFREY_PERFORMER_BP}")

    body, _ = wiring.find_component(bp, "Body")
    if body:
        wiring.set_prop(
            body,
            ["visibility_based_anim_tick_option", "VisibilityBasedAnimTickOption"],
            unreal.VisibilityBasedAnimTickOption.ALWAYS_TICK_POSE_AND_REFRESH_BONES,
        )
        wiring.set_prop(body, ["update_animation_in_editor", "bUpdateAnimationInEditor"], True)
        set_hidden(body, False)
        log("Body: visible (MH hands/neck)")

    bridge, _ = wiring.find_component_by_class(bp, "GodfreyPerformerAnimationBridgeComponent")
    if bridge:
        if wiring.set_prop(bridge, ["b_keep_body_mesh_visible", "bKeepBodyMeshVisible"], True):
            log("Bridge: bKeepBodyMeshVisible=True (blocks Body re-hide)")
        else:
            log("WARN: could not set bKeepBodyMeshVisible — rebuild C++ then re-run")
        wiring.set_prop(
            bridge,
            ["b_debug_force_body_mesh_visible_at_begin_play", "bDebugForceBodyMeshVisibleAtBeginPlay"],
            True,
        )
    else:
        log("WARN: AnimationBridge missing")

    for label in HIDE_LABELS:
        comp, _ = wiring.find_component(bp, label)
        if not comp:
            log(f"skip missing {label}")
            continue
        set_hidden(comp, True)
        log(f"Hidden outfit {label}")

    costume, _ = wiring.find_component(bp, "VictorianCostume")
    if not costume:
        raise RuntimeError("VictorianCostume missing — run dress_victorian_costume_on_godfrey.py first")
    set_hidden(costume, False)
    wiring.set_prop(
        costume,
        ["visibility_based_anim_tick_option", "VisibilityBasedAnimTickOption"],
        unreal.VisibilityBasedAnimTickOption.ALWAYS_TICK_POSE_AND_REFRESH_BONES,
    )
    wiring.set_prop(costume, ["update_animation_in_editor", "bUpdateAnimationInEditor"], True)

    if body:
        try:
            costume.add_tick_prerequisite_component(body)
            log("VictorianCostume tick prerequisite -> Body")
        except Exception as exc:
            log(f"WARN: add_tick_prerequisite_component: {exc}")

    log(f"VictorianCostume visible AnimClass={wiring._anim_class_name(costume)}")

    face, _ = wiring.find_component(bp, "Face")
    if face:
        set_hidden(face, False)
        log("Face kept visible")

    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    if not wiring.save_godfrey_performer_blueprint(bp):
        raise RuntimeError(wiring.BP_SAVE_LOCK_HINT)

    write_report(True)
    log("PASS — Body+hat stay; MHC outfits hidden; bridge will not re-hide Body.")


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        unreal.log_error(f"[FixVictorianVis] {exc}")
        write_report(False)
        sys.exit(1)
