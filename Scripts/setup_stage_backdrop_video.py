"""Point Media Plate at a real mp4 and match Stage_Backdrop's *face*, not its transform.

EDITOR ONLY — Tools > Execute Python Script (Godfrey_World open):
  Scripts/setup_stage_backdrop_video.py

Do not paste Stage_Backdrop's transform onto a Media Plate. Engine Plane faces
local +Z with scale (12, 8, 1) on XY; Media Plate SM_MediaPlateScreen faces
local +X with size on YZ. Copying (12, 8, 1) + the still's rotation puts the
plate edge-on (the yellow sliver).
"""
from __future__ import annotations

import os
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

import godfrey_exhibit_guard  # noqa: E402
import unreal

VIDEO_REL = "Movies/Fremantle Harbour 1890_H264.mp4"
FALLBACK_REL = "Movies/RoundHouseLoop.mp4"
PLANE_MESH = "/MediaPlate/SM_MediaPlateScreen.SM_MediaPlateScreen"


def log(msg: str) -> None:
    unreal.log(f"[BackdropVideo] {msg}")


def resolve_video() -> str:
    content = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_content_dir())
    for rel in (VIDEO_REL, FALLBACK_REL):
        path = os.path.normpath(os.path.join(content, rel))
        if os.path.isfile(path):
            return path
    raise RuntimeError(f"No video at Content/{VIDEO_REL} or Content/{FALLBACK_REL}")


def find_label(label: str):
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    for actor in eas.get_all_level_actors():
        if actor.get_actor_label() == label:
            return actor
    return None


def is_media_plate(actor) -> bool:
    return actor.get_class().get_name().startswith("MediaPlate")


def all_plates():
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    return [a for a in eas.get_all_level_actors() if is_media_plate(a)]


def force_plane_mesh(plate) -> None:
    """Undo 360-sphere mode and reset the letterbox relative scale."""
    comp = plate.get_component_by_class(unreal.MediaPlateComponent)
    if comp:
        try:
            plane = unreal.MediaTextureVisibleMipsTiles.PLANE
            if hasattr(comp, "set_visible_mips_tiles_calculations"):
                comp.set_visible_mips_tiles_calculations(plane)
            else:
                comp.set_editor_property("visible_mips_tiles_calculations", plane)
        except Exception as exc:
            log(f"Could not set Plane mesh mode: {exc}")

    mesh = unreal.load_asset(PLANE_MESH.split(".")[0])
    smc = plate.get_component_by_class(unreal.StaticMeshComponent)
    if mesh and smc:
        smc.set_static_mesh(mesh)
        smc.set_relative_scale3d(unreal.Vector(1.0, 1.0, 1.0))
        log("Set SM_MediaPlateScreen, relative scale (1,1,1)")


def pick_plate(plates):
    """Video is on MediaPlate2. Ignore the sliver extras unless nothing else exists."""
    by_label = {a.get_actor_label(): a for a in plates}
    if "MediaPlate2" in by_label:
        return by_label["MediaPlate2"]
    selected = [
        a for a in unreal.EditorLevelLibrary.get_selected_level_actors() if is_media_plate(a)
    ]
    if selected and selected[0].get_actor_label() != "MediaPlate3":
        return selected[0]
    return plates[0]


def align_plate_to_cine_camera(plate, backdrop, camera) -> None:
    """Sit on Stage_Backdrop. SM_MediaPlateScreen shows the video on -X, so aim -X at the cine camera."""
    loc = backdrop.get_actor_location()
    to_cam = camera.get_actor_location() - loc
    to_cam.z = 0.0
    if to_cam.length() < 1.0:
        to_cam = unreal.Vector(0.0, 1.0, 0.0)
    to_cam = to_cam.normal()
    # +X away from camera → textured -X faces the lens (yaw -90 when camera is at +Y).
    rot = unreal.MathLibrary.make_rot_from_xz(to_cam * -1.0, unreal.Vector(0.0, 0.0, 1.0))
    loc = loc + (to_cam * 1.0)

    scale = backdrop.get_actor_scale3d()
    plate.set_actor_location(loc, False, True)
    plate.set_actor_rotation(rot, False)
    plate.set_actor_scale3d(unreal.Vector(1.0, abs(scale.x), abs(scale.y)))
    log(
        f"{plate.get_actor_label()} loc=({loc.x:.1f},{loc.y:.1f},{loc.z:.1f}) "
        f"rot=({rot.pitch:.1f},{rot.yaw:.1f},{rot.roll:.1f}) scale=(1, {abs(scale.x):.1f}, {abs(scale.y):.1f})"
    )


def main() -> None:
    godfrey_exhibit_guard.reject_headless_spawn()
    world = unreal.EditorLevelLibrary.get_editor_world()
    if world:
        unreal.SystemLibrary.execute_console_command(world, "wp.Editor.LoadAllCells")

    video_path = resolve_video()
    log(f"Video: {video_path}")

    backdrop = find_label("Stage_Backdrop")
    if not backdrop:
        raise RuntimeError("Stage_Backdrop not loaded")

    camera = find_label("Exhibit_CineCamera")
    if not camera:
        raise RuntimeError("Exhibit_CineCamera not loaded")

    plates = all_plates()
    if not plates:
        raise RuntimeError("No MediaPlate in the level. Place one, then re-run this script.")

    plate = pick_plate(plates)
    log(f"Configuring {plate.get_actor_label()} (video plate)")

    force_plane_mesh(plate)
    align_plate_to_cine_camera(plate, backdrop, camera)
    backdrop.set_actor_hidden_in_game(True)

    floor = find_label("Exhibit_Floor")
    if floor:
        floor.set_actor_hidden_in_game(True)
        log("Exhibit_Floor Hidden in Game (collision kept)")

    for extra in plates:
        if extra != plate:
            extra.set_actor_hidden_in_game(True)
            extra.set_actor_enable_collision(False)
            extra.set_is_temporarily_hidden_in_editor(True)
            log(f"Hid extra {extra.get_actor_label()}")

    comp = plate.get_component_by_class(unreal.MediaPlateComponent)
    if not comp:
        raise RuntimeError("MediaPlate has no MediaPlateComponent")

    for name, value in (("play_on_open", True), ("auto_play", True), ("loop", True), ("enable_audio", False)):
        try:
            comp.set_editor_property(name, value)
        except Exception:
            try:
                comp.set_editor_property(f"b_{name}", value)
            except Exception:
                log(f"Could not set {name}")

    # Do not call set_aspect_ratio: it writes StaticMeshComponent relative scale
    # (1, 1, 1/aspect) and would shrink the height we just set on the actor.

    comp.select_external_media(video_path)
    player = comp.get_media_player()
    if player:
        player.set_looping(True)
        player.play()

    log("Done. PIE. Ignore Details 2x2 until playback starts.")


if __name__ == "__main__":
    main()
