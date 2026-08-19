"""Harbour daylight: cubemap wrap + one distant sky panel.

Too dark = cubemap 1.8 only. Blown white = 4500 cd rects 70 cm from his face.
This pass sits in the middle: SkyLight ~5, faint overhead sun (specular 0),
one large fill ~4.5 m in front at a few hundred candelas.

No lights behind MediaPlate2.

EDITOR ONLY — stop PIE, Tools > Execute Python Script:
  Scripts/apply_godfrey_outdoor_lighting.py
Then Ctrl+S.
"""
from __future__ import annotations

import os
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

import godfrey_exhibit_guard  # noqa: E402
import unreal

REPORT = "GodfreyOutdoorLighting.txt"
DAYLIGHT_CUBEMAP = "/Engine/MapTemplates/Sky/DaylightAmbientCubemap.DaylightAmbientCubemap"
STUDIO_OFF_LABELS = (
    "Exhibit_Fill_Key",
    "Exhibit_Fill_Rim",
    "Stage_SkyFill_Front",
    "Stage_BackdropFill",
)
REQUIRED = (
    "Lumen_DirectionalLight",
    "Lumen_SkyLight",
    "Exhibit_CineCamera",
)
SUN_PITCH = -72.0
SUN_INTENSITY = 1.8
SUN_TEMPERATURE = 6200.0
SUN_SOURCE_ANGLE = 12.0
SKY_INTENSITY = 5.0
DISTANT_FILL_INTENSITY = 320.0
DISTANT_FILL_TEMPERATURE = 6800.0
EXPOSURE_BIAS = 0.0
_lines: list[str] = []


def log(msg: str) -> None:
    _lines.append(msg)
    unreal.log(f"[OutdoorLight] {msg}")


def warn(msg: str) -> None:
    _lines.append(f"WARN: {msg}")
    unreal.log_warning(f"[OutdoorLight] {msg}")


def try_set(obj, names, value):
    for name in names if isinstance(names, (list, tuple)) else (names,):
        try:
            obj.set_editor_property(name, value)
            return name
        except Exception:
            continue
    return None


def actors() -> unreal.EditorActorSubsystem:
    return unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def find_by_label(label: str):
    for actor in actors().get_all_level_actors():
        if actor.get_actor_label() == label:
            return actor
    return None


def find_performer():
    preferred = ("BP_Godfrey_Performer", "BP_Kristofer", "BP_MHC_Errol")
    for label in preferred:
        actor = find_by_label(label)
        if actor:
            return actor
    for actor in actors().get_all_level_actors():
        label = actor.get_actor_label()
        if "Godfrey" in label and "Performer" in label:
            return actor
        if "Kristofer" in label and "Light" not in label and "PostProcess" not in label:
            return actor
    return None


def camera_side_basis(performer, camera, plate):
    """Horizontal unit vector from Godfrey toward the cine camera (never toward the plate)."""
    origin = performer.get_actor_location() if performer else unreal.Vector(0.0, 0.0, 90.0)
    if camera:
        cam = camera.get_actor_location()
        to_cam = unreal.Vector(cam.x - origin.x, cam.y - origin.y, 0.0)
    else:
        to_cam = unreal.Vector(0.0, 1.0, 0.0)
    if to_cam.length() < 1.0 and plate:
        plate_loc = plate.get_actor_location()
        to_cam = unreal.Vector(origin.x - plate_loc.x, origin.y - plate_loc.y, 0.0)
    if to_cam.length() < 1.0:
        to_cam = unreal.Vector(0.0, 1.0, 0.0)
    return origin, to_cam.normal()


def in_front(origin, to_cam, forward_cm: float, up_cm: float) -> unreal.Vector:
    forward_cm = max(forward_cm, 40.0)
    return unreal.Vector(
        origin.x + to_cam.x * forward_cm,
        origin.y + to_cam.y * forward_cm,
        origin.z + up_cm,
    )


def lock_manual_exposure(settings) -> None:
    settings.set_editor_property("override_auto_exposure_method", True)
    settings.set_editor_property("auto_exposure_method", unreal.AutoExposureMethod.AEM_MANUAL)
    settings.set_editor_property("override_auto_exposure_bias", True)
    settings.set_editor_property("auto_exposure_bias", EXPOSURE_BIAS)
    try_set(settings, ("override_auto_exposure_apply_physical_camera_exposure",), True)
    try_set(settings, ("auto_exposure_apply_physical_camera_exposure",), False)


def disable_local_light(actor) -> None:
    label = actor.get_actor_label()
    for cls, getter in (
        (unreal.RectLight, unreal.RectLightComponent),
        (unreal.PointLight, unreal.PointLightComponent),
        (unreal.SpotLight, unreal.SpotLightComponent),
    ):
        if not isinstance(actor, cls):
            continue
        comp = actor.get_component_by_class(getter)
        if not comp:
            continue
        try:
            before = float(comp.get_editor_property("intensity"))
        except Exception:
            before = -1.0
        comp.set_intensity(0.0)
        try_set(comp, ("visible",), False)
        try_set(comp, ("hidden_in_game",), True)
        try_set(comp, ("affects_world", "b_affects_world"), False)
        try_set(comp, ("cast_shadows", "cast_shadow"), False)
        log(f"{label}: off (was intensity {before:.1f}) — studio / plate-side light")
        return
    warn(f"{label}: present but not a point/rect/spot light")


def configure_sun(actor, to_cam) -> None:
    """Soft overhead from the visitor sky. Sea-side sun was the cheek specular."""
    comp = actor.get_component_by_class(unreal.DirectionalLightComponent)
    if not comp:
        warn(f"{actor.get_actor_label()} has no DirectionalLightComponent")
        return

    comp.set_mobility(unreal.ComponentMobility.MOVABLE)
    comp.set_intensity(SUN_INTENSITY)
    try_set(comp, ("use_temperature", "b_use_temperature"), True)
    comp.set_temperature(SUN_TEMPERATURE)
    # Hard self-shadow + skin specular is what read as a studio rim.
    comp.set_cast_shadows(False)
    try_set(comp, ("light_source_angle",), SUN_SOURCE_ANGLE)
    try_set(comp, ("atmosphere_sun_light", "b_atmosphere_sun_light"), True)
    try_set(comp, ("affects_world", "b_affects_world"), True)
    try_set(comp, ("visible",), True)
    try_set(comp, ("hidden_in_game",), False)
    try_set(comp, ("specular_scale",), 0.0)

    # Shine from the camera sky toward Godfrey (front/top), not from the plate.
    shine = unreal.Vector(-to_cam.x, -to_cam.y, -0.35)
    look = unreal.MathLibrary.find_look_at_rotation(unreal.Vector(0.0, 0.0, 0.0), shine)
    actor.set_actor_rotation(unreal.Rotator(SUN_PITCH, look.yaw, 0.0), False)
    log(
        f"{actor.get_actor_label()}: lux {SUN_INTENSITY}, {SUN_TEMPERATURE}K, "
        f"source angle {SUN_SOURCE_ANGLE}, pitch {SUN_PITCH}, yaw {look.yaw:.1f}, "
        f"specular 0, shadows off (front sky, not harbour rim)"
    )


def configure_skylight(actor) -> None:
    comp = actor.get_component_by_class(unreal.SkyLightComponent)
    if not comp:
        warn(f"{actor.get_actor_label()} has no SkyLightComponent")
        return

    cube = unreal.load_asset(DAYLIGHT_CUBEMAP.split(".")[0])
    if not cube:
        warn(f"Missing {DAYLIGHT_CUBEMAP} — SkyLight will stay dark")
        return

    source_enum = getattr(unreal.SkyLightSourceType, "SLS_SPECIFIED_CUBEMAP", None)
    if source_enum is None:
        source_enum = getattr(unreal.SkyLightSourceType, "SLS_SPECIFIEDCUBEMAP", None)

    comp.set_mobility(unreal.ComponentMobility.MOVABLE)
    if source_enum is not None:
        try_set(comp, ("source_type",), source_enum)
    if hasattr(comp, "set_cubemap"):
        comp.set_cubemap(cube)
    else:
        try_set(comp, ("cubemap",), cube)
    try_set(comp, ("real_time_capture", "b_real_time_capture"), False)
    try_set(comp, ("lower_hemisphere_is_black", "b_lower_hemisphere_is_black"), False)
    try_set(comp, ("affects_world", "b_affects_world"), True)
    try_set(comp, ("visible",), True)
    try_set(comp, ("hidden_in_game",), False)
    try_set(comp, ("cast_shadows", "cast_shadow"), False)
    comp.set_intensity(SKY_INTENSITY)
    try:
        if hasattr(comp, "recapture_sky"):
            comp.recapture_sky()
    except Exception:
        pass
    log(
        f"{actor.get_actor_label()}: specified cubemap DaylightAmbientCubemap, "
        f"intensity {SKY_INTENSITY}, realtime capture off"
    )


def configure_rect_fill(
    actor,
    origin,
    to_cam,
    forward_cm: float,
    up_cm: float,
    width: float,
    height: float,
    intensity: float,
    temperature: float,
    role: str,
) -> None:
    """Large diffuse panel on the visitor side. Never behind MediaPlate2."""
    comp = actor.get_component_by_class(unreal.RectLightComponent)
    if not comp:
        warn(f"{actor.get_actor_label()} is not a RectLight — skip {role}")
        return

    loc = in_front(origin, to_cam, forward_cm, up_cm)
    aim = unreal.Vector(origin.x, origin.y, origin.z + 95.0)
    look = unreal.MathLibrary.find_look_at_rotation(loc, aim)
    actor.set_actor_location(loc, False, True)
    actor.set_actor_rotation(look, False)

    comp.set_mobility(unreal.ComponentMobility.MOVABLE)
    comp.set_intensity(intensity)
    try_set(comp, ("use_temperature", "b_use_temperature"), True)
    comp.set_temperature(temperature)
    comp.set_source_width(width)
    comp.set_source_height(height)
    comp.set_attenuation_radius(2800.0)
    comp.set_cast_shadows(False)
    try_set(comp, ("specular_scale",), 0.0)
    try_set(comp, ("barn_door_angle",), 88.0)
    try_set(comp, ("barn_door_length",), 0.0)
    try_set(comp, ("visible",), True)
    try_set(comp, ("hidden_in_game",), False)
    try_set(comp, ("affects_world", "b_affects_world"), True)
    log(
        f"{actor.get_actor_label()}: {role} {intensity:.0f} cd, {temperature:.0f}K "
        f"{width:.0f}x{height:.0f} at {loc} (camera side, specular 0)"
    )


def ensure_rect_light(label: str, loc, rot):
    actor = find_by_label(label)
    if actor:
        return actor
    actor = actors().spawn_actor_from_class(unreal.RectLight, loc, rot)
    actor.set_actor_label(label)
    log(f"Spawned {label} on the camera side of MediaPlate2")
    return actor


def exclude_plate_from_lighting_capture() -> None:
    for label in ("MediaPlate2", "MediaPlate", "Stage_Backdrop", "Stage_SkySphere"):
        actor = find_by_label(label)
        if not actor:
            continue
        for comp in actor.get_components_by_class(unreal.PrimitiveComponent):
            try_set(comp, ("hidden_in_scene_capture", "b_hidden_in_scene_capture"), True)
            try_set(comp, ("affect_dynamic_indirect_lighting",), False)
        log(f"{label}: excluded from lighting capture")


def configure_exposure() -> None:
    for actor in actors().get_all_level_actors():
        if isinstance(actor, unreal.PostProcessVolume):
            actor.set_editor_property("unbound", True)
            settings = actor.get_editor_property("settings")
            lock_manual_exposure(settings)
            actor.set_editor_property("settings", settings)
            log(f"{actor.get_actor_label()}: Manual EV {EXPOSURE_BIAS}")
        elif isinstance(actor, unreal.CineCameraActor):
            cine = actor.get_cine_camera_component()
            if not cine:
                continue
            cine.set_editor_property("post_process_blend_weight", 1.0)
            settings = cine.get_editor_property("post_process_settings")
            lock_manual_exposure(settings)
            cine.set_editor_property("post_process_settings", settings)
            log(f"{actor.get_actor_label()}: camera Manual EV {EXPOSURE_BIAS}")


def write_report() -> None:
    path = unreal.Paths.convert_relative_path_to_full(
        unreal.Paths.project_saved_dir() + REPORT
    )
    with open(path, "w", encoding="utf-8") as handle:
        handle.write("\n".join(_lines) + "\n")
    log(f"Report: {path}")


def main() -> None:
    godfrey_exhibit_guard.reject_headless_spawn()
    godfrey_exhibit_guard.require_loaded(REQUIRED)

    performer = find_performer()
    camera = find_by_label("Exhibit_CineCamera")
    plate = (
        find_by_label("MediaPlate2")
        or find_by_label("Stage_Backdrop")
        or find_by_label("Stage_SkySphere")
    )
    if performer:
        log(f"Performer: {performer.get_actor_label()} at {performer.get_actor_location()}")
    else:
        warn("No performer — sky fill uses camera/plate")

    origin, to_cam = camera_side_basis(performer, camera, plate)
    log(f"Camera-side vector: ({to_cam.x:.2f}, {to_cam.y:.2f}) — lights stay on this side of MediaPlate2")

    exclude_plate_from_lighting_capture()

    sun = find_by_label("Lumen_DirectionalLight") or find_by_label("Exhibit_Sun")
    if sun:
        configure_sun(sun, to_cam)
    else:
        warn("No directional sun found")

    sky = find_by_label("Lumen_SkyLight") or find_by_label("Exhibit_SkyLight")
    if sky:
        configure_skylight(sky)
    else:
        warn("No SkyLight found")

    for label in STUDIO_OFF_LABELS:
        actor = find_by_label(label)
        if actor:
            disable_local_light(actor)
        else:
            log(f"{label}: not loaded (ok)")

    fill_loc = in_front(origin, to_cam, 450.0, 240.0)
    fill_rot = unreal.MathLibrary.find_look_at_rotation(
        fill_loc, unreal.Vector(origin.x, origin.y, origin.z + 95.0)
    )
    rect_key = ensure_rect_light("Stage_RectKey", fill_loc, fill_rot)
    configure_rect_fill(
        rect_key,
        origin,
        to_cam,
        450.0,
        240.0,
        900.0,
        700.0,
        DISTANT_FILL_INTENSITY,
        DISTANT_FILL_TEMPERATURE,
        "distant sky (not a face key)",
    )

    configure_exposure()

    try:
        unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
        log("save_dirty_packages OK")
    except Exception as exc:
        warn(f"save_dirty_packages failed: {exc} — Ctrl+S in the editor")

    write_report()
    log("Done. Cubemap 5 + distant 320 cd fill. Face should match the harbour without clipping.")


if __name__ == "__main__":
    main()
