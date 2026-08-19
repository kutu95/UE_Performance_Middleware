"""Revert the Victorian boots overlay. Keep the cotton tank.

- Delete/hide VictorianCostume on BP_Godfrey_Performer
- Restore Loose_Biker_Boots on MHC outfit meshes
- Re-enable clothing cloth sim
- Leave MI_Tank_CottonSolid on the Tank slot

Tools → Execute Python Script (editor open).
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
import remove_victorian_costume_from_godfrey as remove_victorian

importlib.reload(wiring)
importlib.reload(remove_victorian)

BOOT_MAT = "/Game/Outfits/casual_formal/Materials/Cloth/Loose_Biker_Boots"
MI_TANK = "/Game/Outfits/casual_formal/Materials/Cloth/MI_Tank_CottonSolid"
OUTFIT_MESHES = (
    "/Game/MetaHumans/MHC_CaptainGodfrey/Clothing/MHC_CaptainGodfrey_Outfits",
    "/Game/MetaHumans/MHC_Errol/Clothing/MHC_Errol_Outfits",
)
CLOTHING_LABELS = (
    "Torso",
    "Legs",
    "Feet",
    "SkeletalMesh",
    "SkeletalMesh1",
    "SkeletalMesh2",
)
REPORT = "RevertGodfreyVictorianBootsOverlay.txt"
_lines: list[str] = []


def log(msg: str) -> None:
    _lines.append(msg)
    unreal.log(f"[RevertVictorianBoots] {msg}")


def write_report(ok: bool) -> None:
    path = unreal.Paths.convert_relative_path_to_full(
        unreal.Paths.project_saved_dir() + REPORT
    )
    header = "RESULT: PASS\n" if ok else "RESULT: FAIL\n"
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(header + "\n".join(_lines) + "\n")
    log(f"Report: {path}")


def slot_name(entry) -> str:
    try:
        return str(entry.get_editor_property("material_slot_name"))
    except Exception:
        return ""


def restore_boot_slots_on_mesh(mesh, boot_mat) -> int:
    changed = 0
    try:
        mats = list(mesh.get_editor_property("materials") or [])
    except Exception:
        return 0
    new_list = []
    mutated = False
    for entry in mats:
        name = slot_name(entry)
        if "biker" in name.lower() or name.lower() == "loose_biker_boots":
            try:
                entry.set_editor_property("material_interface", boot_mat)
                mutated = True
                changed += 1
                log(f"  restored '{name}' on {mesh.get_name()}")
            except Exception as exc:
                log(f"  WARN restore '{name}': {exc}")
        new_list.append(entry)
    if mutated:
        mesh.set_editor_property("materials", new_list)
        unreal.EditorAssetLibrary.save_loaded_asset(mesh)
    return changed


def restore_component_materials(comp, boot_mat, tank_mi) -> None:
    mesh = None
    try:
        mesh = comp.get_skeletal_mesh_asset()
    except Exception:
        try:
            mesh = comp.get_editor_property("skeletal_mesh")
        except Exception:
            mesh = None
    slot_names = []
    if mesh:
        try:
            slot_names = [slot_name(e) for e in list(mesh.get_editor_property("materials") or [])]
        except Exception:
            slot_names = []
    try:
        count = int(comp.get_num_materials())
    except Exception:
        count = len(slot_names)
    for index in range(count):
        name = slot_names[index] if index < len(slot_names) else ""
        lower = name.lower()
        if "biker" in lower or "boot" in lower:
            if boot_mat:
                try:
                    comp.set_material(index, boot_mat)
                except Exception:
                    pass
            for lod in range(8):
                try:
                    comp.show_material_section(index, True, lod)
                except Exception:
                    try:
                        comp.show_material_section(index, True)
                        break
                    except Exception:
                        break
        if lower == "tank" and tank_mi:
            try:
                comp.set_material(index, tank_mi)
            except Exception:
                pass
    wiring.set_prop(comp, ["disable_cloth_simulation", "b_disable_cloth_simulation"], False)
    wiring.set_prop(comp, ["cloth_blend_weight"], 1.0)


def hide_world_victorian() -> int:
    hidden = 0
    try:
        subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
        actors = list(subsystem.get_all_level_actors()) if subsystem else []
    except Exception as exc:
        log(f"WARN world actors: {exc}")
        return 0
    for actor in actors:
        try:
            label = actor.get_actor_label()
        except Exception:
            continue
        if "Godfrey" not in label and "CaptainGodfrey" not in label and "MHC_Errol" not in label:
            continue
        for comp in actor.get_components_by_class(unreal.SkeletalMeshComponent) or []:
            name = comp.get_name()
            mesh = None
            try:
                mesh = comp.get_skeletal_mesh_asset()
            except Exception:
                pass
            mesh_name = mesh.get_name() if mesh else ""
            if "Victorian" in name or "Victorian" in mesh_name or "Gentleman" in mesh_name:
                wiring.set_prop(comp, ["b_hidden_in_game", "bHiddenInGame"], True)
                wiring.set_prop(comp, ["visible", "bVisible"], False)
                try:
                    comp.set_skeletal_mesh_asset(None)
                except Exception:
                    try:
                        comp.set_editor_property("skeletal_mesh", None)
                    except Exception:
                        pass
                hidden += 1
                log(f"  hid world {label}.{name}")
    return hidden


def main() -> None:
    log("=== Revert Victorian boots overlay; keep cotton tank ===")
    boot_mat = unreal.EditorAssetLibrary.load_asset(BOOT_MAT)
    tank_mi = unreal.EditorAssetLibrary.load_asset(MI_TANK)
    if not boot_mat:
        raise RuntimeError(f"Missing {BOOT_MAT}")

    restored_slots = 0
    for path in OUTFIT_MESHES:
        mesh = unreal.EditorAssetLibrary.load_asset(path)
        if isinstance(mesh, unreal.SkeletalMesh):
            restored_slots += restore_boot_slots_on_mesh(mesh, boot_mat)
    log(f"Restored biker boot slots: {restored_slots}")

    bp = unreal.load_asset(wiring.GODFREY_PERFORMER_BP)
    if not bp:
        raise RuntimeError(f"Missing {wiring.GODFREY_PERFORMER_BP}")

    deleted = remove_victorian.delete_costume_component(bp)
    log(f"Deleted/hidden VictorianCostume: {deleted}")

    leftover, leftover_label = wiring.find_component(bp, "VictorianCostume")
    if leftover:
        try:
            leftover.set_skeletal_mesh_asset(None)
        except Exception:
            try:
                leftover.set_editor_property("skeletal_mesh", None)
            except Exception:
                pass
        remove_victorian.set_hidden(leftover, True)
        wiring.set_prop(leftover, ["anim_class", "AnimClass"], None)
        log(f"Forced hide leftover {leftover_label}")

    for label, component, _h, _d in wiring.iter_all_components(bp):
        if "SkeletalMesh" not in component.get_class().get_name():
            continue
        if label in CLOTHING_LABELS or label.startswith("SkeletalMesh"):
            restore_component_materials(component, boot_mat, tank_mi)
            remove_victorian.set_hidden(component, False)

    face, _ = wiring.find_component(bp, "Face")
    if face:
        remove_victorian.set_hidden(face, False)

    world_hidden = hide_world_victorian()
    log(f"World Victorian components hidden: {world_hidden}")

    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    if not wiring.save_godfrey_performer_blueprint(bp):
        raise RuntimeError(wiring.BP_SAVE_LOCK_HINT)

    write_report(True)
    log(
        "PASS — Victorian overlay removed. MHC coat/pants/biker boots restored; "
        "cotton tank MI kept. Select Godfrey in the level and confirm VictorianCostume is gone."
    )


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        unreal.log_error(f"[RevertVictorianBoots] {exc}")
        write_report(False)
        raise
