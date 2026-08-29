"""Replace the placed Godfrey_World performer with a fresh BP_Godfrey_Performer.

That is the MetaHuman swap: destroy the old World Partition instance (it still
carries Hair_S_Clean / blonde Beard_L_Messy overrides) and spawn a new actor
from the migrated Blueprint CDO (MH_RealityErrol body/face/grooms + ACE stack).

EDITOR ONLY — never UnrealEditor-Cmd. Headless spawn/destroy splits WP cells.

Open Godfrey_World, stop PIE, load exhibit cells, then:
  Tools → Execute Python Script → this file
  Ctrl+S the level
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
import godfrey_exhibit_guard  # noqa: E402

importlib.reload(wiring)

REPORT = "SwapGodfreyWorldMhcPerformer.txt"
LEVEL_PATH = "/Game/Godfrey_World"
PERFORMER_LABEL = "BP_Godfrey_Performer"
_lines: list[str] = []


def log(msg: str) -> None:
    _lines.append(msg)
    unreal.log(f"[SwapGodfreyMHC] {msg}")


def write_report(ok: bool) -> None:
    path = unreal.Paths.convert_relative_path_to_full(
        unreal.Paths.project_saved_dir() + REPORT
    )
    header = "RESULT: PASS\n" if ok else "RESULT: FAIL\n"
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(header + "\n".join(_lines) + "\n")
    log(f"Report: {path}")


def _actor_class_path(actor) -> str:
    try:
        return actor.get_class().get_path_name()
    except Exception:
        return str(actor.get_class())


def _groom_summary(actor) -> str:
    parts = []
    try:
        grooms = list(actor.get_components_by_class(unreal.GroomComponent) or [])
    except Exception:
        grooms = []
    for comp in grooms:
        name = str(comp.get_name())
        asset = None
        try:
            asset = comp.get_editor_property("groom_asset")
        except Exception:
            pass
        path = asset.get_path_name() if asset else "(none)"
        parts.append(f"{name}={path}")
    return "; ".join(parts) if parts else "(no grooms)"


def _is_performer(actor) -> bool:
    if not actor:
        return False
    try:
        label = actor.get_actor_label()
        name = actor.get_name()
        tags = list(actor.tags)
    except Exception:
        return False
    if (
        label == PERFORMER_LABEL
        or PERFORMER_LABEL in name
        or "BP_MHC_Errol" in label
        or "BP_MH_RealityErrol" in label
        or "BP_MHC_CaptainGodfrey" in label
        or "BP_Kristofer" in label
        or wiring.GODFREY_CHARACTER_TAG in tags
    ):
        return True
    try:
        ace_cls = unreal.load_class(None, wiring.ACE_CURVE_SOURCE_CLASS)
        if ace_cls and actor.get_component_by_class(ace_cls):
            return True
    except Exception:
        pass
    return False


def find_performers() -> list:
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    found = []
    for actor in eas.get_all_level_actors() or []:
        if _is_performer(actor):
            found.append(actor)
            log(
                f"Found: label={actor.get_actor_label()} name={actor.get_name()} "
                f"class={_actor_class_path(actor)} loc={actor.get_actor_location()} "
                f"grooms={_groom_summary(actor)}"
            )
    return found


def capture_transform(actor):
    loc = actor.get_actor_location()
    rot = actor.get_actor_rotation()
    scale = actor.get_actor_scale3d()
    folder = None
    try:
        folder = actor.get_folder_path()
    except Exception:
        folder = None
    return loc, rot, scale, folder


def replace_with_fresh_instance(bp) -> object:
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    performers = find_performers()
    if not performers:
        raise RuntimeError(
            "No performer actor loaded. Open Godfrey_World, run wp.Editor.LoadAllCells, "
            "fly to the exhibit dock, then re-run this script."
        )

    primary = None
    for actor in performers:
        if actor.get_actor_label() == PERFORMER_LABEL:
            primary = actor
            break
    if primary is None:
        primary = performers[0]
        log(f"WARN: replacing {primary.get_actor_label()} (expected {PERFORMER_LABEL})")

    loc, rot, scale, folder = capture_transform(primary)
    log(f"Keep transform loc={loc} rot={rot} scale={scale} folder={folder}")

    generated = bp.generated_class() if hasattr(bp, "generated_class") else None
    if not generated:
        raise RuntimeError(f"No generated class for {wiring.GODFREY_PERFORMER_BP}")

    for actor in performers:
        label = actor.get_actor_label()
        eas.destroy_actor(actor)
        log(f"Destroyed stale instance {label}")

    actor = eas.spawn_actor_from_class(generated, loc, rot)
    if not actor:
        raise RuntimeError("spawn_actor_from_class failed")

    actor.set_actor_label(PERFORMER_LABEL)
    actor.set_actor_scale3d(scale)
    actor.set_actor_hidden_in_game(False)
    if folder:
        try:
            actor.set_folder_path(folder)
        except Exception:
            pass
    tags = list(actor.tags)
    if wiring.GODFREY_CHARACTER_TAG not in tags:
        tags.append(wiring.GODFREY_CHARACTER_TAG)
        actor.tags = tags
    log(f"Spawned fresh {PERFORMER_LABEL} class={_actor_class_path(actor)}")
    log(f"New grooms: {_groom_summary(actor)}")
    return actor


def assert_fresh_look(actor) -> None:
    summary = _groom_summary(actor)
    banned = ("Hair_S_Clean", "Beard_L_Messy", "Mustache_L_Handlebar")
    hits = [token for token in banned if token in summary]
    if hits:
        raise RuntimeError(
            f"Fresh actor still has old grooms ({hits}): {summary}. "
            "Blueprint CDO is stale — re-run migrate_reality_errol_to_godfrey_performer.py first."
        )
        if "Hair_S_Casual" not in summary:
            log(f"WARN: expected Hair_S_Casual on fresh actor, got: {summary}")
    body = None
    try:
        for mesh in actor.get_components_by_class(unreal.SkeletalMeshComponent) or []:
            if str(mesh.get_name()) == "Body":
                body = mesh
                break
    except Exception:
        body = None
    if body:
        try:
            skm = body.get_skeletal_mesh_asset()
            path = skm.get_path_name() if skm else "(none)"
        except Exception:
            path = "(none)"
        log(f"Fresh Body mesh: {path}")
        if "SKM_MH_RealityErrol_BodyMesh" not in path:
            raise RuntimeError(f"Fresh actor body is not RealityErrol: {path}")


def save_level() -> None:
    try:
        les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
        les.save_current_level()
        log("LevelEditorSubsystem.save_current_level")
    except Exception as exc:
        log(f"save_current_level: {exc}")
    try:
        world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
        unreal.EditorLoadingAndSavingUtils.save_map(world, LEVEL_PATH)
        log(f"save_map {LEVEL_PATH}")
    except Exception as exc:
        log(f"save_map: {exc}")
    try:
        unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
        log("Saved dirty packages")
    except Exception as exc:
        log(f"save_dirty_packages: {exc}")


def main() -> None:
    godfrey_exhibit_guard.reject_headless_spawn()

    bp = unreal.load_asset(wiring.GODFREY_PERFORMER_BP)
    if not bp:
        raise RuntimeError(
            f"Missing {wiring.GODFREY_PERFORMER_BP} — run migrate_reality_errol_to_godfrey_performer.py first"
        )

    body, _ = wiring.find_component(bp, "Body")
    face, _ = wiring.find_component(bp, "Face")
    if not body or not face:
        raise RuntimeError("Migrated performer missing Body/Face")
    ace, _ = wiring.find_component_by_class(bp, "ACEAudioCurveSourceComponent")
    if not ace:
        raise RuntimeError("Migrated performer missing ACEAudioCurveSource — re-run migrate script")

    try:
        body_mesh = body.get_skeletal_mesh_asset()
        body_path = body_mesh.get_path_name() if body_mesh else "(none)"
    except Exception:
        body_path = "(none)"
    log(f"Blueprint Body: {body_path}")
    if "SKM_MH_RealityErrol_BodyMesh" not in body_path:
        raise RuntimeError(
            f"Blueprint is not RealityErrol yet ({body_path}). "
            "Run migrate_reality_errol_to_godfrey_performer.py first."
        )

    world = unreal.EditorLevelLibrary.get_editor_world()
    if world:
        unreal.SystemLibrary.execute_console_command(world, "wp.Editor.LoadAllCells")
        log("wp.Editor.LoadAllCells")

    godfrey_exhibit_guard.require_loaded((PERFORMER_LABEL,))

    actor = replace_with_fresh_instance(bp)
    assert_fresh_look(actor)
    save_level()

    write_report(True)
    log(
        "PASS — old placed MetaHuman destroyed; fresh BP_Godfrey_Performer spawned "
        "at the same dock transform. Ctrl+S if prompted, then PIE."
    )


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        unreal.log_error(f"[SwapGodfreyMHC] {exc}")
        write_report(False)
        sys.exit(1)
