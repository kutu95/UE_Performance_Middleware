"""Pull Godfrey out of blown-out white in PIE.

PIE uses Exhibit_CineCamera. Its own exposure (often Manual + physical camera)
overrides Kristofer_PostProcess. Stage_BackdropFill is 120000 nits — that
washes him out.

Does not move MediaPlate / Stage_Backdrop / floor.

EDITOR ONLY — stop PIE, then Tools > Execute Python Script:
  Scripts/fix_exhibit_playback_exposure.py
"""
from __future__ import annotations

import os
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

import godfrey_exhibit_guard  # noqa: E402
import unreal

# Negative = darker. Physical-camera Manual at bias 0 is still white with the fill lights.
EXPOSURE_BIAS = -2.0
RECT_LIGHT_CAP = 2500.0
POINT_LIGHT_CAP = 50.0
DIRECTIONAL_CAP = 4.0


def log(msg: str) -> None:
    unreal.log(f"[PlaybackExposure] {msg}")


def lock_manual_exposure(settings) -> None:
    settings.set_editor_property("override_auto_exposure_method", True)
    settings.set_editor_property("auto_exposure_method", unreal.AutoExposureMethod.AEM_MANUAL)
    settings.set_editor_property("override_auto_exposure_bias", True)
    settings.set_editor_property("auto_exposure_bias", EXPOSURE_BIAS)
    try:
        settings.set_editor_property("override_auto_exposure_apply_physical_camera_exposure", True)
        settings.set_editor_property("auto_exposure_apply_physical_camera_exposure", False)
    except Exception:
        pass


def cap_light(actor, comp, cap: float, kind: str) -> None:
    try:
        intensity = float(comp.get_editor_property("intensity"))
    except Exception:
        return
    if intensity <= cap:
        return
    comp.set_intensity(cap)
    log(f"{actor.get_actor_label()}: {kind} intensity {intensity:.0f} -> {cap:.0f}")


def main() -> None:
    godfrey_exhibit_guard.reject_headless_spawn()
    world = unreal.EditorLevelLibrary.get_editor_world()
    if world:
        unreal.SystemLibrary.execute_console_command(world, "wp.Editor.LoadAllCells")

    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    for actor in eas.get_all_level_actors():
        if isinstance(actor, unreal.SkyLight):
            comp = actor.get_component_by_class(unreal.SkyLightComponent)
            if comp:
                comp.set_real_time_capture(False)
                log(f"{actor.get_actor_label()}: Real Time Capture OFF")

        if isinstance(actor, unreal.PostProcessVolume):
            actor.set_editor_property("unbound", True)
            settings = actor.get_editor_property("settings")
            lock_manual_exposure(settings)
            actor.set_editor_property("settings", settings)
            log(f"{actor.get_actor_label()}: Manual EV {EXPOSURE_BIAS}, physical camera off")

        if isinstance(actor, unreal.CineCameraActor):
            cine = actor.get_cine_camera_component()
            if cine:
                cine.set_editor_property("post_process_blend_weight", 1.0)
                settings = cine.get_editor_property("post_process_settings")
                lock_manual_exposure(settings)
                cine.set_editor_property("post_process_settings", settings)
                log(f"{actor.get_actor_label()}: camera Manual EV {EXPOSURE_BIAS}, physical camera off")

        if isinstance(actor, unreal.RectLight):
            cap_light(actor, actor.get_component_by_class(unreal.RectLightComponent), RECT_LIGHT_CAP, "rect")
        elif isinstance(actor, unreal.PointLight):
            cap_light(actor, actor.get_component_by_class(unreal.PointLightComponent), POINT_LIGHT_CAP, "point")
        elif isinstance(actor, unreal.DirectionalLight):
            cap_light(
                actor,
                actor.get_component_by_class(unreal.DirectionalLightComponent),
                DIRECTIONAL_CAP,
                "directional",
            )

    log("Done. PIE again. If still hot, drop Exhibit_CineCamera Exposure Compensation further.")


if __name__ == "__main__":
    main()
