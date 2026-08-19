"""Fix Casual Formal sleeve cloth spikes, then overlay Victorian Gentleman boots.

- Restore original Tank material on Chaos Cloth / Outfit Asset (keep cotton MI on
  visible MHC outfit meshes).
- Disable cloth sim on Godfrey clothing so cuff verts stop stretching.
- Add VictorianCostume as boots-only (component tag VictorianBootsOnly) and hide
  Loose_Biker_Boots sections. Coat, pants, and cotton tank stay.

Requires a Live Coding / editor compile of GodfreyCostumeRetargetAnimInstance
so boots-only material stripping survives PIE.

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
import dress_victorian_costume_on_godfrey as dress
import fix_victorian_scale_align as scale_align

importlib.reload(wiring)
importlib.reload(dress)
importlib.reload(scale_align)

PARENT_TANK = "/Game/Outfits/casual_formal/Materials/Cloth/Tank"
MI_TANK = "/Game/Outfits/casual_formal/Materials/Cloth/MI_Tank_CottonSolid"
BOOT_SLOT_TOKENS = ("loose_biker_boots", "biker_boot", "boot")
CLOTH_RESTORE = (
    "/Game/Outfits/casual_formal/ClothAssets/CA_a",
    "/Game/Outfits/casual_formal/ClothAssets/CA_b",
    "/Game/Outfits/OA_Casual_formal",
)
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
BOOTS_TAG = "VictorianBootsOnly"
REPORT = "FixGodfreySleevesAndVictorianBoots.txt"
_lines: list[str] = []


def log(msg: str) -> None:
    _lines.append(msg)
    unreal.log(f"[SleevesBoots] {msg}")


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


def mat_iface(entry):
    try:
        return entry.get_editor_property("material_interface")
    except Exception:
        return None


def is_boot_slot(name: str) -> bool:
    lower = name.lower()
    return any(tok in lower for tok in BOOT_SLOT_TOKENS)


def replace_slot_material(obj, match_names: set[str], new_mat, label: str) -> int:
    changed = 0
    try:
        mats = list(obj.get_editor_property("materials") or [])
    except Exception:
        return 0
    new_list = []
    for entry in mats:
        name = slot_name(entry)
        iface = mat_iface(entry)
        iface_name = iface.get_name() if iface else ""
        if name in match_names or iface_name in match_names:
            try:
                entry.set_editor_property("material_interface", new_mat)
                changed += 1
                log(f"  {label} slot '{name}' -> {new_mat.get_name() if new_mat else 'None'}")
            except Exception as exc:
                log(f"  WARN {label} slot '{name}': {exc}")
        new_list.append(entry)
    if changed:
        try:
            obj.set_editor_property("materials", new_list)
            unreal.EditorAssetLibrary.save_loaded_asset(obj)
        except Exception as exc:
            log(f"  WARN save {label}: {exc}")
            return 0
    return changed


def restore_cloth_tank() -> int:
    tank = unreal.EditorAssetLibrary.load_asset(PARENT_TANK)
    if not tank:
        raise RuntimeError(f"Missing {PARENT_TANK}")
    changed = 0
    for path in CLOTH_RESTORE:
        asset = unreal.EditorAssetLibrary.load_asset(path)
        if not asset:
            log(f"WARN missing {path}")
            continue
        changed += replace_slot_material(asset, {"Tank", "MI_Tank_CottonSolid"}, tank, asset.get_name())
    return changed


def hide_biker_boots_on_outfits() -> int:
    changed = 0
    for path in OUTFIT_MESHES:
        mesh = unreal.EditorAssetLibrary.load_asset(path)
        if not isinstance(mesh, unreal.SkeletalMesh):
            continue
        try:
            mats = list(mesh.get_editor_property("materials") or [])
        except Exception:
            continue
        new_list = []
        mutated = False
        for entry in mats:
            name = slot_name(entry)
            if is_boot_slot(name):
                try:
                    entry.set_editor_property("material_interface", None)
                    mutated = True
                    changed += 1
                    log(f"  hide boot slot '{name}' on {mesh.get_name()}")
                except Exception as exc:
                    log(f"  WARN hide boot on {mesh.get_name()}: {exc}")
            new_list.append(entry)
        if mutated:
            mesh.set_editor_property("materials", new_list)
            unreal.EditorAssetLibrary.save_loaded_asset(mesh)
    return changed


def hide_boot_sections_on_component(comp) -> int:
    changed = 0
    mesh = None
    try:
        mesh = comp.get_editor_property("skeletal_mesh")
    except Exception:
        pass
    slot_indices = []
    if mesh:
        try:
            mats = list(mesh.get_editor_property("materials") or [])
            for index, entry in enumerate(mats):
                if is_boot_slot(slot_name(entry)):
                    slot_indices.append(index)
        except Exception:
            pass
    try:
        count = int(comp.get_num_materials())
    except Exception:
        count = 0
    for index in range(count):
        current = None
        try:
            current = comp.get_material(index)
        except Exception:
            pass
        name = current.get_name() if current else ""
        if index in slot_indices or is_boot_slot(name) or name in ("Loose_Biker_Boots",):
            try:
                comp.set_material(index, None)
                changed += 1
            except Exception:
                pass
            for lod in range(8):
                try:
                    comp.show_material_section(index, False, lod)
                except Exception:
                    try:
                        comp.show_material_section(index, False)
                        break
                    except Exception:
                        break
    return changed


def disable_cloth_on_component(comp) -> None:
    wiring.set_prop(comp, ["disable_cloth_simulation", "b_disable_cloth_simulation"], True)
    wiring.set_prop(comp, ["cloth_blend_weight"], 0.0)
    try:
        pp = comp.get_post_process_instance()
    except Exception:
        pp = None
    if not pp:
        return
    for prop in (
        "Enable Rigid Body Simulation",
        "Enable Control Rig",
        "enable_rigid_body_simulation",
        "enable_control_rig",
    ):
        try:
            pp.set_editor_property(prop, False)
        except Exception:
            continue


def tag_boots_only(comp) -> None:
    tags = []
    try:
        tags = list(comp.get_editor_property("component_tags") or [])
    except Exception:
        tags = []
    names = {str(t) for t in tags}
    if BOOTS_TAG not in names:
        tags.append(BOOTS_TAG)
        try:
            comp.set_editor_property("component_tags", tags)
            log(f"Tagged {comp.get_name()} {BOOTS_TAG}")
        except Exception as exc:
            log(f"WARN component_tags: {exc}")


def ensure_victorian_boots(bp) -> object:
    costume = dress.ensure_costume_component(bp)
    tag_boots_only(costume)
    wiring.set_prop(costume, ["b_hidden_in_game", "bHiddenInGame"], False)
    wiring.set_prop(costume, ["visible", "bVisible"], True)
    wiring.set_prop(
        costume,
        ["visibility_based_anim_tick_option", "VisibilityBasedAnimTickOption"],
        unreal.VisibilityBasedAnimTickOption.ALWAYS_TICK_POSE_AND_REFRESH_BONES,
    )
    wiring.set_prop(costume, ["update_animation_in_editor", "bUpdateAnimationInEditor"], True)
    return costume


def apply_scale(bp) -> None:
    body_mesh = unreal.load_asset(scale_align.BODY_MESH)
    costume_mesh = unreal.load_asset(scale_align.COSTUME_MESH)
    if not body_mesh or not costume_mesh:
        log("WARN scale meshes missing")
        return
    _bmin, _bmax, bheight = scale_align.mesh_bounds_z(body_mesh)
    _cmin, _cmax, cheight = scale_align.mesh_bounds_z(costume_mesh)
    if cheight < 1.0:
        log("WARN costume height invalid")
        return
    raw = bheight / cheight
    scale = max(0.65, min(1.05, raw))
    log(f"Victorian boots scale={scale:.4f} (raw={raw:.4f})")
    scale_align.apply_costume_scale(bp, scale, 0.0)
    rtg = unreal.load_asset(scale_align.RETARGETER)
    if rtg:
        try:
            scale_align.auto_align_retarget(rtg)
        except Exception as exc:
            log(f"WARN auto_align_retarget: {exc}")


def patch_performer(bp, mic_keep) -> int:
    changed = 0
    for label, component, _h, _d in wiring.iter_all_components(bp):
        if "SkeletalMesh" not in component.get_class().get_name():
            continue
        if label in CLOTHING_LABELS or label.startswith("SkeletalMesh"):
            disable_cloth_on_component(component)
            changed += hide_boot_sections_on_component(component)
            # Keep cotton tank on clothing.
            try:
                count = int(component.get_num_materials())
            except Exception:
                count = 0
            for index in range(count):
                current = component.get_material(index)
                if current and current.get_name() == "Tank" and mic_keep:
                    try:
                        component.set_material(index, mic_keep)
                    except Exception:
                        pass
    return changed


def main() -> None:
    log("=== Sleeve spike revert + Victorian boots overlay ===")
    tank_mi = unreal.EditorAssetLibrary.load_asset(MI_TANK)
    restored = restore_cloth_tank()
    log(f"Restored original Tank on cloth/OA slots: {restored}")
    hidden = hide_biker_boots_on_outfits()
    log(f"Hidden biker boot slots on MHC outfits: {hidden}")

    bp = unreal.load_asset(wiring.GODFREY_PERFORMER_BP)
    if not bp:
        raise RuntimeError(f"Missing {wiring.GODFREY_PERFORMER_BP}")

    costume = ensure_victorian_boots(bp)
    log(f"VictorianCostume mesh={costume.get_editor_property('skeletal_mesh')}")
    apply_scale(bp)
    patched = patch_performer(bp, tank_mi)
    log(f"Performer clothing boot hides / cloth-off: {patched}")

    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    if not wiring.save_godfrey_performer_blueprint(bp):
        raise RuntimeError(wiring.BP_SAVE_LOCK_HINT)

    ok = restored > 0 and costume is not None
    write_report(ok)
    if not ok:
        raise RuntimeError("Cloth restore or VictorianCostume failed")
    log(
        "PASS — compile UnrealPerformer if C++ changed, then PIE. "
        "Sleeves should lose spikes; Victorian shoes overlay MHC coat/pants/tank. "
        "If shoes float, tune VictorianCostume RelativeScale3D / Location."
    )


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        unreal.log_error(f"[SleevesBoots] {exc}")
        write_report(False)
        raise
