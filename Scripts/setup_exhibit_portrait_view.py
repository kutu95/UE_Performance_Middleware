"""Portrait exhibit view: cine camera is the play camera, 1200x1920.

The harbour clip is 16:9. The plate is sized to the cine frustum height so
the clip fills the portrait frame top-to-bottom (sides crop). Do not stretch.

EDITOR ONLY — stop PIE, Tools > Execute Python Script:
  Scripts/setup_exhibit_portrait_view.py
Then Ctrl+S. Play via New Editor Window.
"""
from __future__ import annotations

import math
import os
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

import godfrey_exhibit_guard  # noqa: E402
import unreal

WINDOW_W = 1200
WINDOW_H = 1920
ASPECT = WINDOW_W / float(WINDOW_H)  # 0.625
SENSOR_W = 15.0
SENSOR_H = 24.0
FOCAL_MM = 35.0
VIDEO_ASPECT = 16.0 / 9.0
MESH_UU = 100.0
# Tiny overscan so a 1-pixel gap does not show at the top/bottom of the frame.
HEIGHT_OVERSCAN = 1.01


def log(msg: str) -> None:
    unreal.log(f"[PortraitView] {msg}")


def find_label(label: str):
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    for actor in eas.get_all_level_actors():
        if actor.get_actor_label() == label:
            return actor
    return None


def configure_play_window() -> None:
    settings = unreal.get_default_object(unreal.LevelEditorPlaySettings)
    settings.set_editor_property("new_window_width", WINDOW_W)
    settings.set_editor_property("new_window_height", WINDOW_H)
    try:
        settings.set_editor_property("center_new_window", True)
    except Exception:
        pass
    try:
        settings.set_editor_property(
            "last_executed_play_mode_type",
            unreal.PlayModeType.PLAY_MODE_IN_EDITOR_FLOATING,
        )
    except Exception:
        try:
            settings.set_editor_property("last_executed_play_mode_type", 1)
        except Exception as exc:
            log(f"Could not set Play in New Window: {exc}")
    try:
        settings.set_editor_property(
            "additional_launch_parameters",
            f"-ResX={WINDOW_W} -ResY={WINDOW_H} -Windowed",
        )
    except Exception:
        pass
    settings.save_config()
    log(f"PIE New Window {WINDOW_W}x{WINDOW_H}")


def configure_cine_camera():
    camera = find_label("Exhibit_CineCamera")
    if not camera:
        raise RuntimeError("Exhibit_CineCamera not loaded — run wp.Editor.LoadAllCells")
    cine = camera.get_cine_camera_component()
    if not cine:
        raise RuntimeError("Exhibit_CineCamera has no CineCameraComponent")

    filmback = unreal.CameraFilmbackSettings()
    filmback.set_editor_property("sensor_width", SENSOR_W)
    filmback.set_editor_property("sensor_height", SENSOR_H)
    cine.set_filmback(filmback)
    cine.set_constraint_aspect_ratio(True)
    cine.set_aspect_ratio(ASPECT)
    try:
        cine.set_editor_property("current_focal_length", FOCAL_MM)
    except Exception:
        pass

    for names, value in (
        (("auto_activate_for_player",), unreal.AutoReceiveInput.PLAYER0),
        (("AutoActivateForPlayer",), unreal.AutoReceiveInput.PLAYER0),
    ):
        try:
            camera.set_editor_property(names[0], value)
            log("Exhibit_CineCamera AutoActivateForPlayer = Player0")
            break
        except Exception:
            continue

    log(f"Exhibit_CineCamera filmback {SENSOR_W}x{SENSOR_H} mm, constrain {ASPECT:.3f}")
    return camera, cine


def size_media_plate_fill_portrait_height(camera) -> None:
    """True 16:9 plate filling the portrait frustum height. Left/right crop is expected."""
    plate = find_label("MediaPlate2") or find_label("MediaPlate")
    backdrop = find_label("Stage_Backdrop")
    if not plate:
        log("No MediaPlate2 — skip plate sizing")
        return
    if not backdrop:
        raise RuntimeError("Stage_Backdrop not loaded")

    comp = plate.get_component_by_class(unreal.MediaPlateComponent)
    if comp:
        try:
            comp.set_is_aspect_ratio_auto(False)
        except Exception:
            try:
                comp.set_editor_property("is_aspect_ratio_auto", False)
            except Exception:
                pass
        try:
            comp.set_letterbox_aspect_ratio(0.0)
        except Exception:
            pass

    smc = plate.get_component_by_class(unreal.StaticMeshComponent)
    if smc:
        # Auto-aspect relative (1,1,9/16) plus actor scale was squashing the image.
        smc.set_relative_scale3d(unreal.Vector(1.0, 1.0, 1.0))

    cam_loc = camera.get_actor_location()
    plate_loc = backdrop.get_actor_location()
    forward = camera.get_actor_forward_vector()
    dist = unreal.Vector.dot_product(plate_loc - cam_loc, forward)
    if dist < 50.0:
        dist = (plate_loc - cam_loc).length()
    dist = max(dist, 50.0)

    vfov = 2.0 * math.atan((SENSOR_H * 0.5) / FOCAL_MM)
    frustum_h = 2.0 * dist * math.tan(vfov * 0.5)
    plate_h = frustum_h * HEIGHT_OVERSCAN
    plate_w = plate_h * VIDEO_ASPECT

    to_cam = cam_loc - plate_loc
    to_cam.z = 0.0
    if to_cam.length() < 1.0:
        to_cam = unreal.Vector(0.0, 1.0, 0.0)
    to_cam = to_cam.normal()
    rot = unreal.MathLibrary.make_rot_from_xz(to_cam * -1.0, unreal.Vector(0.0, 0.0, 1.0))
    loc = plate_loc + (to_cam * 1.0)

    plate.set_actor_location(loc, False, True)
    plate.set_actor_rotation(rot, False)
    plate.set_actor_scale3d(unreal.Vector(1.0, plate_w / MESH_UU, plate_h / MESH_UU))
    log(
        f"{plate.get_actor_label()} 16:9 fill-height at dist={dist:.0f} "
        f"size=({plate_w:.0f}x{plate_h:.0f}) scale=(1, {plate_w / MESH_UU:.2f}, {plate_h / MESH_UU:.2f})"
    )


def main() -> None:
    godfrey_exhibit_guard.reject_headless_spawn()
    world = unreal.EditorLevelLibrary.get_editor_world()
    if world:
        unreal.SystemLibrary.execute_console_command(world, "wp.Editor.LoadAllCells")

    configure_play_window()
    camera, _cine = configure_cine_camera()
    size_media_plate_fill_portrait_height(camera)
    log("Done. Save. Harbour stays 16:9 and fills portrait height (sides crop).")


if __name__ == "__main__":
    main()
