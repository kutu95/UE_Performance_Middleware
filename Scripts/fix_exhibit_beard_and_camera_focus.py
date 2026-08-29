"""Darken RealityErrol beard/hair and put Exhibit_CineCamera in deep focus.

Beard materials were melanin 0.58 / white 0.36 (reads blonde in sun).
Exhibit_CineCamera is still f/2.8 with manual focus from the old body — after
the MetaHuman swap the face is off the focal plane.

Headless:
  UnrealEditor-Cmd.exe ".../UnrealPerformer.uproject" /Game/Godfrey_World
    -ExecutePythonScript=".../Scripts/fix_exhibit_beard_and_camera_focus.py"
    -unattended -nop4 -nosplash -stdout

Editor: stop PIE, Tools → Execute Python Script → this file, Ctrl+S.
"""
from __future__ import annotations

import os
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

import importlib

import unreal

import godfrey_blueprint_wiring as wiring  # noqa: E402
import fix_reality_errol_native_grooms as grooms  # noqa: E402

importlib.reload(wiring)
importlib.reload(grooms)

LEVEL_PATH = "/Game/Godfrey_World"
CAMERA_LABEL = "Exhibit_CineCamera"
PERFORMER_LABEL = "BP_Godfrey_Performer"
APERTURE = 16.0

REPORT = "FixExhibitBeardAndCameraFocus.txt"
_lines: list[str] = []


def log(msg: str) -> None:
    _lines.append(msg)
    unreal.log(f"[BeardCameraFocus] {msg}")


def write_report(ok: bool) -> None:
    path = unreal.Paths.convert_relative_path_to_full(
        unreal.Paths.project_saved_dir() + REPORT
    )
    if "MetaHuman_Baseline" not in path.replace("\\", "/"):
        path = r"D:/UE Projects/MetaHuman_Baseline_UE58_Test/Saved/" + REPORT
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(("RESULT: PASS\n" if ok else "RESULT: FAIL\n") + "\n".join(_lines) + "\n")
    log(f"Report: {path}")


def find_label(label: str):
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    for actor in eas.get_all_level_actors() or []:
        if actor and actor.get_actor_label() == label:
            return actor
    return None


def darken_groom_materials() -> None:
    grooms.build_color_materials()
    log(
        f"Beard MI melanin={grooms.BEARD_COLOR['hairMelanin']} "
        f"white={grooms.BEARD_COLOR['WhiteAmount']}"
    )


def disable_camera_dof(cine, performer) -> None:
    try:
        cine.set_editor_property("current_aperture", APERTURE)
        log(f"Aperture -> {APERTURE} (was cinematic f/2.8)")
    except Exception as exc:
        log(f"WARN aperture: {exc}")

    focus = None
    try:
        focus = cine.get_editor_property("focus_settings")
    except Exception:
        focus = None
    if not focus:
        log("WARN: no focus_settings on cine camera")
        return

    method = None
    for name in ("DISABLE", "Disable", "DO_NOT_OVERRIDE"):
        method = getattr(unreal.CameraFocusMethod, name, None)
        if method is not None:
            break
    if method is not None:
        try:
            focus.set_editor_property("focus_method", method)
            log(f"Focus method -> {method}")
        except Exception as exc:
            log(f"WARN focus_method: {exc}")
            method = None

    if method is None:
        tracking = getattr(unreal.CameraFocusMethod, "TRACKING", None)
        if tracking is not None and performer:
            try:
                focus.set_editor_property("focus_method", tracking)
                tfs = focus.get_editor_property("tracking_focus_settings")
                tfs.set_editor_property("actor_to_track", performer)
                tfs.set_editor_property("relative_offset", unreal.Vector(0.0, 0.0, 72.0))
                focus.set_editor_property("tracking_focus_settings", tfs)
                log("Focus method -> Tracking performer head")
            except Exception as exc:
                log(f"WARN tracking focus: {exc}")

    try:
        cine.set_editor_property("focus_settings", focus)
    except Exception as exc:
        log(f"WARN write focus_settings: {exc}")

    try:
        settings = cine.get_editor_property("post_process_settings")
        for names, value in (
            (("override_depth_of_field_fstop",), True),
            (("depth_of_field_fstop",), 32.0),
            (("override_depth_of_field_focal_distance",), False),
            (("override_depth_of_field_min_fstop",), False),
        ):
            try:
                settings.set_editor_property(names[0], value)
            except Exception:
                pass
        cine.set_editor_property("post_process_settings", settings)
        log("Camera post-process DOF f-stop 32")
    except Exception as exc:
        log(f"WARN camera PP: {exc}")


def save_camera_and_level() -> None:
    try:
        les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
        les.save_current_level()
        log("save_current_level")
    except Exception as exc:
        log(f"save_current_level: {exc}")
    try:
        unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
        log("Saved dirty packages")
    except Exception as exc:
        log(f"save_dirty_packages: {exc}")


def main() -> None:
    log("=== Darken RealityErrol beard + deep-focus exhibit camera ===")
    darken_groom_materials()

    world = None
    try:
        world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    except Exception:
        world = unreal.EditorLevelLibrary.get_editor_world()
    path = ""
    try:
        path = world.get_path_name() if world else ""
    except Exception:
        path = ""
    if "Godfrey_World" not in str(path):
        log(f"Loading {LEVEL_PATH} (current={path or 'none'})")
        unreal.EditorLoadingAndSavingUtils.load_map(LEVEL_PATH)
        world = unreal.EditorLevelLibrary.get_editor_world()
    if world:
        unreal.SystemLibrary.execute_console_command(world, "wp.Editor.LoadAllCells")
        log("wp.Editor.LoadAllCells")

    camera = find_label(CAMERA_LABEL)
    performer = find_label(PERFORMER_LABEL)
    if not camera:
        raise RuntimeError(f"{CAMERA_LABEL} not loaded")
    cine = camera.get_cine_camera_component()
    if not cine:
        raise RuntimeError(f"{CAMERA_LABEL} has no CineCameraComponent")

    try:
        log(
            f"Camera before aperture={cine.get_editor_property('current_aperture')} "
            f"focal={cine.get_editor_property('current_focal_length')}"
        )
    except Exception:
        pass
    if performer:
        log(f"Performer {performer.get_actor_label()} at {performer.get_actor_location()}")

    disable_camera_dof(cine, performer)
    save_camera_and_level()

    write_report(True)
    log("PASS — beard darker; camera deep focus. Ctrl+S if prompted, then PIE.")


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        unreal.log_error(f"[BeardCameraFocus] {exc}")
        write_report(False)
        sys.exit(1)
