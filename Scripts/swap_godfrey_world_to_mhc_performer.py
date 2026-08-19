"""Editor-only: ensure Godfrey_World places the migrated BP_Godfrey_Performer (MHC Errol shell).

World Partition rule: do NOT run this via UnrealEditor-Cmd headless spawn/destroy.
Open Godfrey_World, load exhibit cells, then:
  Tools → Execute Python Script → this file

If the level actor is already BP_Godfrey_Performer (same soft path after asset replace),
this only re-applies the GodfreyCharacter tag and logs mesh/component sanity.
If a stale Kristofer/Bridge instance remains under another label, replace it in-editor.
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

REPORT = "SwapGodfreyWorldMhcPerformer.txt"
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


def main() -> None:
    bp = unreal.load_asset(wiring.GODFREY_PERFORMER_BP)
    if not bp:
        raise RuntimeError(
            f"Missing {wiring.GODFREY_PERFORMER_BP} — run migrate_errol_to_godfrey_performer.py first"
        )

    # Confirm MHC Errol shell (Body/Face present; meshes should be SKM_MHC_Errol_* after migrate).
    body, _ = wiring.find_component(bp, "Body")
    face, _ = wiring.find_component(bp, "Face")
    if not body or not face:
        raise RuntimeError("Migrated performer missing Body/Face")

    try:
        body_mesh = body.get_editor_property("SkeletalMesh")
        face_mesh = face.get_editor_property("SkeletalMesh")
        body_path = body_mesh.get_path_name() if body_mesh else "(none)"
        face_path = face_mesh.get_path_name() if face_mesh else "(none)"
        log(f"Body mesh: {body_path}")
        log(f"Face mesh: {face_path}")
        if "CaptainGodfrey" in body_path or "CaptainGodfrey" in face_path:
            log("WARN: performer still references CaptainGodfrey meshes — re-run Errol migrate after Assemble")
        elif "MHC_Errol" not in body_path and "Errol" not in body_path:
            log(f"WARN: unexpected body mesh path (expected SKM_MHC_Errol_*): {body_path}")
    except Exception as exc:
        log(f"WARN: could not read Body/Face mesh soft paths: {exc}")

    ace, _ = wiring.find_component_by_class(bp, "ACEAudioCurveSourceComponent")
    if not ace:
        raise RuntimeError("Migrated performer missing ACEAudioCurveSource — re-run migrate script")

    log(f"Asset OK: {wiring.GODFREY_PERFORMER_BP}")
    log(f"Body AnimClass: {wiring._anim_class_name(body)}")
    log(f"Face AnimClass: {wiring._anim_class_name(face)}")

    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    if not eas:
        raise RuntimeError("EditorActorSubsystem unavailable — open Godfrey_World in the editor")

    performers = []
    for actor in eas.get_all_level_actors():
        if not actor:
            continue
        label = actor.get_actor_label()
        name = actor.get_name()
        cls_path = _actor_class_path(actor)
        is_performer = (
            label == "BP_Godfrey_Performer"
            or "BP_Godfrey_Performer" in name
            or "BP_MHC_Errol" in label
            or "BP_MHC_CaptainGodfrey" in label
            or "BP_Kristofer" in label
            or wiring.GODFREY_CHARACTER_TAG in list(actor.tags)
        )
        if not is_performer:
            # Also match by ACE component on placed actors.
            try:
                if actor.get_component_by_class(
                    unreal.load_class(None, wiring.ACE_CURVE_SOURCE_CLASS)
                ):
                    is_performer = True
            except Exception:
                pass
        if is_performer:
            performers.append(actor)
            log(f"Found candidate: label={label} name={name} class={cls_path} loc={actor.get_actor_location()}")

    if not performers:
        raise RuntimeError(
            "No performer actor loaded in the current world. "
            "Open Godfrey_World, load the exhibit region (wp.Editor.LoadAllCells or fly there), "
            "then re-run this script. Or place BP_Godfrey_Performer manually and tag GodfreyCharacter."
        )

    # Prefer exact BP_Godfrey_Performer label.
    primary = None
    for actor in performers:
        if actor.get_actor_label() == "BP_Godfrey_Performer":
            primary = actor
            break
    if primary is None:
        primary = performers[0]
        log(f"WARN: using first candidate label={primary.get_actor_label()} (expected BP_Godfrey_Performer)")

    tags = list(primary.tags)
    if wiring.GODFREY_CHARACTER_TAG not in tags:
        tags.append(wiring.GODFREY_CHARACTER_TAG)
        primary.tags = tags
        log(f"Applied tag {wiring.GODFREY_CHARACTER_TAG}")
    else:
        log(f"Tag {wiring.GODFREY_CHARACTER_TAG} already present")

    # Soft-path class check: after asset replace, placed actor should resolve to new MHC BP.
    cls_path = _actor_class_path(primary)
    if (
        "BP_Godfrey_Performer" not in cls_path
        and "BP_MHC_Errol" not in cls_path
        and "BP_MHC_CaptainGodfrey" not in cls_path
    ):
        log(
            f"WARN: actor class path looks unexpected: {cls_path}. "
            "Delete the old actor and place /Game/MetaHumans/Godfrey/BP_Godfrey_Performer at the same transform."
        )
    else:
        log(f"Actor class path OK: {cls_path}")

    # Save the actor package if dirty (editor session).
    try:
        unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
        log("Saved dirty packages")
    except Exception as exc:
        log(f"WARN: save_dirty_packages: {exc}")

    write_report(True)
    log(
        "PASS — level performer tagged. PIE: queue TTS from Brain; expect audible + lip sync "
        "([ACE sync] First curve weights applied). Cloth should stay stable (MHC outfits)."
    )


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        unreal.log_error(f"[SwapGodfreyMHC] {exc}")
        write_report(False)
        sys.exit(1)
