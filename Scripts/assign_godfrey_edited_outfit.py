"""Put Godfrey on the in-editor outfit mesh, not the broken CinchedBoots import.

CinchedBoots failed to merge with metahuman_base_skel, so coat sleeves stay in
the rest pose while the body animation moves the arms through the coat.

Prefers MHC_CaptainGodfrey_Outfits1 (Triangle Edit / Lattice duplicate).
Falls back to original MHC_CaptainGodfrey_Outfits.

Tools → Execute Python Script → this file (stop Play first).
"""
from __future__ import annotations

import os
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

import importlib

import unreal

import godfrey_blueprint_wiring as wiring

importlib.reload(wiring)

OUTFITS1 = "/Game/MetaHumans/MHC_CaptainGodfrey/Clothing/MHC_CaptainGodfrey_Outfits1"
OUTFITS = "/Game/MetaHumans/MHC_CaptainGodfrey/Clothing/MHC_CaptainGodfrey_Outfits"
CINCHED = "SKM_Godfrey_Outfits_CinchedBoots"
SKIP_LABELS = {"Body", "Face"}
REPORT = "AssignGodfreyEditedOutfit.txt"
_lines: list[str] = []


def log(msg: str) -> None:
    _lines.append(msg)
    unreal.log(f"[EditedOutfit] {msg}")


def write_report(ok: bool) -> None:
    path = unreal.Paths.convert_relative_path_to_full(
        unreal.Paths.project_saved_dir() + REPORT
    )
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(("RESULT: PASS\n" if ok else "RESULT: FAIL\n") + "\n".join(_lines) + "\n")
    log(f"Report: {path}")


def mesh_name(comp) -> str:
    mesh = None
    try:
        mesh = comp.get_skeletal_mesh_asset()
    except Exception:
        pass
    if not mesh:
        try:
            mesh = comp.get_editor_property("skeletal_mesh")
        except Exception:
            mesh = None
    return mesh.get_name() if mesh else ""


def assign_mesh(comp, mesh) -> bool:
    try:
        comp.set_skeletal_mesh_asset(mesh)
        return True
    except Exception:
        pass
    try:
        comp.set_editor_property("skeletal_mesh", mesh)
        return True
    except Exception:
        return False


def pin_cloth(comp) -> None:
    wiring.set_prop(comp, ["disable_cloth_simulation", "b_disable_cloth_simulation"], True)
    wiring.set_prop(comp, ["cloth_blend_weight"], 0.0)
    wiring.set_prop(comp, ["allow_cloth_actors", "b_allow_cloth_actors"], False)


def is_clothing(label: str, current: str) -> bool:
    if label in SKIP_LABELS:
        return False
    if current == CINCHED:
        return True
    if "Outfits" in current or "Cinched" in current:
        return True
    return False


def pick_target() -> unreal.SkeletalMesh:
    for path in (OUTFITS1, OUTFITS):
        mesh = unreal.load_asset(path)
        if isinstance(mesh, unreal.SkeletalMesh):
            log(f"Target mesh: {path}")
            return mesh
    raise RuntimeError(
        "Neither MHC_CaptainGodfrey_Outfits1 nor MHC_CaptainGodfrey_Outfits was found."
    )


def assign_on_blueprint(bp, mesh) -> int:
    body, _ = wiring.find_component(bp, "Body")
    assigned = 0
    for label, component, _h, _d in wiring.iter_all_components(bp):
        if "SkeletalMesh" not in component.get_class().get_name():
            continue
        current = mesh_name(component)
        if not is_clothing(label, current):
            continue
        if assign_mesh(component, mesh):
            pin_cloth(component)
            if body:
                try:
                    component.set_leader_pose_component(body, True, True)
                    log(f"  BP {label}: leader pose -> Body")
                except Exception as exc:
                    log(f"  WARN {label} leader pose: {exc}")
            assigned += 1
            log(f"  BP {label}: {current} -> {mesh.get_name()}")
    return assigned


def assign_on_world(mesh) -> int:
    assigned = 0
    try:
        subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
        actors = list(subsystem.get_all_level_actors()) if subsystem else []
    except Exception as exc:
        log(f"WARN world: {exc}")
        return 0
    for actor in actors:
        try:
            label = actor.get_actor_label()
        except Exception:
            continue
        if "Godfrey" not in label and "CaptainGodfrey" not in label:
            continue
        comps = list(actor.get_components_by_class(unreal.SkeletalMeshComponent) or [])
        body = next((c for c in comps if c.get_name() == "Body"), None)
        for comp in comps:
            name = comp.get_name()
            current = mesh_name(comp)
            if not is_clothing(name, current):
                continue
            if assign_mesh(comp, mesh):
                pin_cloth(comp)
                if body:
                    try:
                        comp.set_leader_pose_component(body, True, True)
                    except Exception:
                        pass
                assigned += 1
                log(f"  world {label}.{name}: {current} -> {mesh.get_name()}")
    return assigned


def main() -> None:
    log("=== Assign edited MHC outfit (replace CinchedBoots) ===")
    mesh = pick_target()
    bp = unreal.load_asset(wiring.GODFREY_PERFORMER_BP)
    if not bp:
        raise RuntimeError(f"Missing {wiring.GODFREY_PERFORMER_BP}")
    bp_n = assign_on_blueprint(bp, mesh)
    world_n = assign_on_world(mesh)
    log(f"Assigned on BP: {bp_n}, world: {world_n}")
    if bp_n == 0 and world_n == 0:
        raise RuntimeError(
            "No clothing component found. Open BP_Godfrey_Performer and check the "
            "Skeletal Mesh slot still says CinchedBoots or an Outfits mesh."
        )
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    if not wiring.save_godfrey_performer_blueprint(bp):
        log(wiring.BP_SAVE_LOCK_HINT)
    write_report(True)
    log("PASS — stop Play, then Play again. Coat sleeves should follow the arms.")


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        unreal.log_error(f"[EditedOutfit] {exc}")
        write_report(False)
        raise
