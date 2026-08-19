"""Pin Godfrey coat-cuff spikes to the skinned pose.

Those black needles are Chaos Cloth stretching Long_slim_coat wrist verts.
The Victorian overlay is not involved. Cotton tank MI stays on visible meshes.

What this does:
- Restore original Tank on CA_a / CA_b / OA_Casual_formal (cloth data only)
- Disable ChaosClothComponent simulation (skinned pose, not last spiked pose)
- Disable SkeletalMesh clothing sim / blend / max-distance
- Apply on BP_Godfrey_Performer and live world actors so the viewport updates

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

importlib.reload(wiring)

PARENT_TANK = "/Game/Outfits/casual_formal/Materials/Cloth/Tank"
MI_TANK = "/Game/Outfits/casual_formal/Materials/Cloth/MI_Tank_CottonSolid"
CLOTH_RESTORE = (
    "/Game/Outfits/casual_formal/ClothAssets/CA_a",
    "/Game/Outfits/casual_formal/ClothAssets/CA_b",
    "/Game/Outfits/OA_Casual_formal",
)
SKIP_LABELS = {
    "face",
    "hair",
    "hair_s_clean",
    "eyebrows",
    "eyelashes",
    "beard",
    "mustache",
    "fuzz",
    "lodsync",
}
REPORT = "FixGodfreyCoatSleeveSpikes.txt"
_lines: list[str] = []


def log(msg: str) -> None:
    _lines.append(msg)
    unreal.log(f"[SleeveSpikes] {msg}")


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


def class_name(obj) -> str:
    try:
        return obj.get_class().get_name()
    except Exception:
        return type(obj).__name__


def is_chaos_cloth(comp) -> bool:
    name = class_name(comp)
    return "ChaosCloth" in name or name in ("ClothComponent", "ChaosClothComponent")


def should_skip_label(label: str) -> bool:
    lower = (label or "").lower()
    if lower in SKIP_LABELS:
        return True
    if "victorian" in lower:
        return True
    if "groom" in lower:
        return True
    return False


def read_prop(obj, names: list[str]):
    for name in names:
        try:
            return obj.get_editor_property(name)
        except Exception:
            continue
    return None


def call_if(obj, method: str, *args) -> bool:
    fn = getattr(obj, method, None)
    if not callable(fn):
        return False
    try:
        fn(*args)
        return True
    except Exception:
        return False


def dump_comp(label: str, comp) -> None:
    mesh = None
    for getter in ("get_skinned_asset", "get_skeletal_mesh_asset", "get_asset"):
        fn = getattr(comp, getter, None)
        if callable(fn):
            try:
                mesh = fn()
                if mesh:
                    break
            except Exception:
                continue
    mesh_name = mesh.get_name() if mesh else "-"
    log(
        f"  {label} class={class_name(comp)} mesh={mesh_name} "
        f"disableCloth={read_prop(comp, ['disable_cloth_simulation', 'b_disable_cloth_simulation'])} "
        f"blend={read_prop(comp, ['cloth_blend_weight', 'blend_weight'])} "
        f"maxDist={read_prop(comp, ['cloth_max_distance_scale'])} "
        f"allowActors={read_prop(comp, ['allow_cloth_actors', 'b_allow_cloth_actors'])} "
        f"enableSim={read_prop(comp, ['enable_simulation', 'b_enable_simulation'])} "
        f"simEditor={read_prop(comp, ['simulate_in_editor', 'b_simulate_in_editor'])}"
    )


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
        try:
            mats = list(asset.get_editor_property("materials") or [])
        except Exception:
            log(f"  {asset.get_name()} has no materials array")
            continue
        new_list = []
        mutated = False
        for entry in mats:
            name = slot_name(entry)
            iface = mat_iface(entry)
            iface_name = iface.get_name() if iface else ""
            if name == "Tank" or iface_name in ("Tank", "MI_Tank_CottonSolid"):
                try:
                    entry.set_editor_property("material_interface", tank)
                    mutated = True
                    changed += 1
                    log(f"  {asset.get_name()} slot '{name}' -> Tank")
                except Exception as exc:
                    log(f"  WARN {asset.get_name()} slot '{name}': {exc}")
            new_list.append(entry)
        if mutated:
            try:
                asset.set_editor_property("materials", new_list)
                unreal.EditorAssetLibrary.save_loaded_asset(asset)
            except Exception as exc:
                log(f"  WARN save {asset.get_name()}: {exc}")
    return changed


def keep_cotton_tank(comp, tank_mi) -> int:
    if not tank_mi:
        return 0
    changed = 0
    try:
        count = int(comp.get_num_materials())
    except Exception:
        return 0
    for index in range(count):
        current = None
        try:
            current = comp.get_material(index)
        except Exception:
            continue
        name = current.get_name() if current else ""
        if name == "Tank":
            try:
                comp.set_material(index, tank_mi)
                changed += 1
            except Exception:
                pass
    return changed


def disable_chaos_cloth(comp) -> list[str]:
    applied = []
    # Skinned pose (blend 0). Do not SuspendSimulation — that keeps the spiked last pose.
    if wiring.set_prop(comp, ["blend_weight"], 0.0):
        applied.append("blend_weight=0")
    if call_if(comp, "set_enable_simulation", False):
        applied.append("set_enable_simulation(False)")
    elif wiring.set_prop(comp, ["enable_simulation", "b_enable_simulation"], False):
        applied.append("enable_simulation=False")
    if call_if(comp, "set_simulate_in_editor", False):
        applied.append("set_simulate_in_editor(False)")
    else:
        wiring.set_prop(comp, ["simulate_in_editor", "b_simulate_in_editor"], False)
    if call_if(comp, "force_next_update_teleport_and_reset"):
        applied.append("teleport_reset")
    call_if(comp, "mark_render_state_dirty")
    return applied


def disable_skm_cloth(comp) -> list[str]:
    applied = []
    if wiring.set_prop(comp, ["disable_cloth_simulation", "b_disable_cloth_simulation"], True):
        applied.append("disable_cloth_simulation")
    if wiring.set_prop(comp, ["cloth_blend_weight"], 0.0):
        applied.append("cloth_blend_weight=0")
    if call_if(comp, "set_cloth_max_distance_scale", 0.0):
        applied.append("set_cloth_max_distance_scale(0)")
    elif wiring.set_prop(comp, ["cloth_max_distance_scale"], 0.0):
        applied.append("cloth_max_distance_scale=0")
    if call_if(comp, "set_allow_cloth_actors", False):
        applied.append("set_allow_cloth_actors(False)")
    elif wiring.set_prop(comp, ["allow_cloth_actors", "b_allow_cloth_actors"], False):
        applied.append("allow_cloth_actors=False")
    if wiring.set_prop(comp, ["disable_rigid_body_anim_node", "b_disable_rigid_body_anim_node"], True):
        applied.append("disable_rigid_body_anim_node")
    if call_if(comp, "force_cloth_next_update_teleport_and_reset"):
        applied.append("skm_teleport_reset")
    if call_if(comp, "recreate_clothing_actors"):
        applied.append("recreate_clothing_actors")
    try:
        pp = comp.get_post_process_instance()
    except Exception:
        pp = None
    if pp:
        for prop in (
            "Enable Rigid Body Simulation",
            "Enable Control Rig",
            "enable_rigid_body_simulation",
            "enable_control_rig",
        ):
            try:
                pp.set_editor_property(prop, False)
                applied.append(f"pp.{prop}=False")
            except Exception:
                continue
    call_if(comp, "mark_render_state_dirty")
    return applied


def pin_component(label: str, comp, tank_mi) -> bool:
    if should_skip_label(label):
        return False
    cls = class_name(comp)
    if "SkeletalMesh" not in cls and "SkinnedMesh" not in cls and not is_chaos_cloth(comp):
        return False
    dump_comp(label, comp)
    applied = []
    if is_chaos_cloth(comp):
        applied.extend(disable_chaos_cloth(comp))
    if "SkeletalMesh" in cls:
        applied.extend(disable_skm_cloth(comp))
    tank_hits = keep_cotton_tank(comp, tank_mi)
    if tank_hits:
        applied.append(f"tank_mi x{tank_hits}")
    if applied:
        log(f"    pinned {label}: {', '.join(applied)}")
        return True
    log(f"    no cloth props on {label}")
    return False


def pin_blueprint(bp, tank_mi) -> int:
    pinned = 0
    log("--- BP components ---")
    for label, component, _h, _d in wiring.iter_all_components(bp):
        if pin_component(label, component, tank_mi):
            pinned += 1
    return pinned


def pin_world_actors(tank_mi) -> int:
    pinned = 0
    log("--- World actors ---")
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
        comps = []
        for cls_name in ("ChaosClothComponent", "SkinnedMeshComponent", "SkeletalMeshComponent", "ActorComponent"):
            cls = getattr(unreal, cls_name, None)
            if cls is None:
                continue
            try:
                found = list(actor.get_components_by_class(cls) or [])
            except Exception:
                found = []
            comps.extend(found)
        seen = set()
        unique = []
        for comp in comps:
            key = id(comp)
            if key in seen:
                continue
            seen.add(key)
            unique.append(comp)
        log(f"Actor {label} comps={len(unique)}")
        for comp in unique:
            name = comp.get_name()
            if pin_component(f"{label}.{name}", comp, tank_mi):
                pinned += 1
    return pinned


def main() -> None:
    log("=== Pin coat-cuff Chaos Cloth spikes (keep cotton tank, MHC boots) ===")
    tank_mi = unreal.EditorAssetLibrary.load_asset(MI_TANK)
    restored = restore_cloth_tank()
    log(f"Restored original Tank on cloth/OA slots: {restored}")

    bp = unreal.load_asset(wiring.GODFREY_PERFORMER_BP)
    if not bp:
        raise RuntimeError(f"Missing {wiring.GODFREY_PERFORMER_BP}")

    bp_pinned = pin_blueprint(bp, tank_mi)
    world_pinned = pin_world_actors(tank_mi)
    log(f"Pinned BP components: {bp_pinned}")
    log(f"Pinned world components: {world_pinned}")

    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    if not wiring.save_godfrey_performer_blueprint(bp):
        raise RuntimeError(wiring.BP_SAVE_LOCK_HINT)

    ok = bp_pinned > 0 or world_pinned > 0
    write_report(ok)
    if not ok:
        raise RuntimeError("No clothing/cloth components were pinned")
    log(
        "PASS — click Godfrey in the viewport. Cuff needles should snap back to the coat. "
        "Cotton tank and biker boots stay. If spikes return in PIE, say so."
    )


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        unreal.log_error(f"[SleeveSpikes] {exc}")
        write_report(False)
        raise
