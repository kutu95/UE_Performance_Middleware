"""Exhibit_Floor visible in the editor, invisible in PIE/game.

Collision stays. Does not move MediaPlate.

EDITOR ONLY — stop PIE, Tools > Execute Python Script:
  Scripts/hide_exhibit_floor_in_play.py
Then Ctrl+S. The checkerboard should return in the viewport.
PIE hides it so the video shows through.
"""
from __future__ import annotations

import os
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

import godfrey_exhibit_guard  # noqa: E402
import unreal

FLOOR_MAT = "/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"


def log(msg: str) -> None:
    unreal.log(f"[SeeThroughFloor] {msg}")


def try_set(obj, names, value) -> None:
    for name in names if isinstance(names, (list, tuple)) else (names,):
        try:
            obj.set_editor_property(name, value)
            return
        except Exception:
            continue


def looks_like_exhibit_floor(actor) -> bool:
    label = actor.get_actor_label()
    if label in ("Stage_Backdrop", "Stage_SkySphere"):
        return False
    if "Floor" in label:
        return True
    if not isinstance(actor, unreal.StaticMeshActor):
        return False
    scale = actor.get_actor_scale3d()
    return abs(scale.x) >= 20.0 and abs(scale.y) >= 20.0 and abs(scale.z) <= 2.0


def restore_editor_visible_hide_in_play(actor) -> None:
    actor.set_actor_hidden_in_game(True)
    actor.set_actor_enable_collision(True)

    comp = actor.get_component_by_class(unreal.StaticMeshComponent)
    if not comp:
        log(f"WARN: {actor.get_actor_label()} has no mesh")
        return

    mat = unreal.load_asset(FLOOR_MAT)
    if mat:
        comp.set_material(0, mat)

    # Editor draws this. PIE uses Hidden in Game (and C++ after rebuild).
    try_set(comp, ("hidden_in_game",), True)
    try_set(comp, ("visible",), True)
    try_set(comp, ("cast_shadow",), False)
    try_set(comp, ("render_in_main_pass", "b_render_in_main_pass"), True)
    try_set(comp, ("render_in_depth_pass", "b_render_in_depth_pass"), True)
    try_set(comp, ("use_as_occluder", "b_use_as_occluder"), True)
    try_set(comp, ("affect_dynamic_indirect_lighting",), False)
    try_set(comp, ("affect_distance_field_lighting",), False)
    try_set(comp, ("visible_in_ray_tracing",), False)
    try:
        comp.set_collision_enabled(unreal.CollisionEnabled.QUERY_AND_PHYSICS)
    except Exception:
        pass
    log(f"{actor.get_actor_label()}: visible in editor, Hidden in Game")


def main() -> None:
    godfrey_exhibit_guard.reject_headless_spawn()
    world = unreal.EditorLevelLibrary.get_editor_world()
    if world:
        unreal.SystemLibrary.execute_console_command(world, "wp.Editor.LoadAllCells")

    count = 0
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    for actor in eas.get_all_level_actors():
        if looks_like_exhibit_floor(actor):
            restore_editor_visible_hide_in_play(actor)
            count += 1

    if count == 0:
        raise RuntimeError("No Exhibit_Floor loaded. Run wp.Editor.LoadAllCells, then retry.")
    log(f"Updated {count} floor actor(s). Save the level. Floor stays in the viewport; PIE hides it.")


if __name__ == "__main__":
    main()
