"""Import the Blender-cinched outfit FBX and put it on BP_Godfrey_Performer.

Keeps original MHC_CaptainGodfrey_Outfits as rollback. Cotton tank MI stays.
Coat / pants geometry were not edited in Blender (boot material verts only).

Requires Saved/BootCinch/MHC_CaptainGodfrey_Outfits_CinchedBoots.fbx
from Scripts/run_blender_cinch_godfrey_boots.ps1

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

SOURCE_OUTFIT = "/Game/MetaHumans/MHC_CaptainGodfrey/Clothing/MHC_CaptainGodfrey_Outfits"
MI_TANK = "/Game/Outfits/casual_formal/Materials/Cloth/MI_Tank_CottonSolid"
DEST_DIR = "/Game/Outfits/casual_formal/Cinched"
DEST_NAME = "SKM_Godfrey_Outfits_CinchedBoots"
DEST_PATH = f"{DEST_DIR}/{DEST_NAME}"
FBX_REL = os.path.join("Saved", "BootCinch", "MHC_CaptainGodfrey_Outfits_CinchedBoots.fbx")
REPORT = "ApplyGodfreyCinchedBoots.txt"
_lines: list[str] = []


def log(msg: str) -> None:
    _lines.append(msg)
    unreal.log(f"[CinchedBoots] {msg}")


def write_report(ok: bool) -> None:
    path = unreal.Paths.convert_relative_path_to_full(
        unreal.Paths.project_saved_dir() + REPORT
    )
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(("RESULT: PASS\n" if ok else "RESULT: FAIL\n") + "\n".join(_lines) + "\n")
    log(f"Report: {path}")


def project_root() -> str:
    return unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()).rstrip("/\\")


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


def ensure_dir(path: str) -> None:
    if not unreal.EditorAssetLibrary.does_directory_exist(path):
        unreal.EditorAssetLibrary.make_directory(path)


def source_skeleton(mesh: unreal.SkeletalMesh):
    try:
        return mesh.get_editor_property("skeleton")
    except Exception:
        return None


def fbx_has_leaf_end_bones(fbx_path: str) -> bool:
    try:
        with open(fbx_path, "rb") as handle:
            data = handle.read()
    except OSError:
        return False
    return b"_end" in data and (
        b"foot_l_end" in data or b"calf_l_end" in data or b"thigh_l_end" in data
    )


def _content_type(name: str):
    enum = getattr(unreal, "FBXImportContentType", None)
    if enum is None:
        return None
    for attr in (name, name.upper(), "FBXICT_" + name.upper()):
        if hasattr(enum, attr):
            return getattr(enum, attr)
    return None


def restore_source_outfit(bp, source_mesh, tank_mi) -> None:
    """Put original MHC outfits back so the cinched asset can be rebuilt."""
    hits = 0
    for label, component, _h, _d in wiring.iter_all_components(bp):
        if "SkeletalMesh" not in component.get_class().get_name():
            continue
        if mesh_name(component) in OUTFIT_MESH_NAMES:
            if assign_mesh(component, source_mesh):
                keep_tank_on_component(component, tank_mi)
                hits += 1
    assign_on_world(source_mesh, tank_mi)
    log(f"Restored original outfit on {hits} BP component(s)")


def duplicate_source_outfit() -> unreal.SkeletalMesh:
    ensure_dir(DEST_DIR)
    if unreal.EditorAssetLibrary.does_asset_exist(DEST_PATH):
        try:
            unreal.EditorAssetLibrary.delete_asset(DEST_PATH)
            log(f"Deleted previous {DEST_PATH}")
        except Exception as exc:
            log(f"WARN delete {DEST_PATH}: {exc}")
    copied = unreal.EditorAssetLibrary.duplicate_asset(SOURCE_OUTFIT, DEST_PATH)
    mesh = copied if isinstance(copied, unreal.SkeletalMesh) else unreal.load_asset(DEST_PATH)
    if not isinstance(mesh, unreal.SkeletalMesh):
        raise RuntimeError(f"Failed to duplicate {SOURCE_OUTFIT} -> {DEST_PATH}")
    log(f"Duplicated original outfit -> {mesh.get_path_name()}")
    return mesh


def import_fbx_onto(fbx_path: str, skeleton, content_type_name: str) -> list[str]:
    ui = unreal.FbxImportUI()
    ui.set_editor_property("import_mesh", True)
    ui.set_editor_property("import_animations", False)
    ui.set_editor_property("import_materials", False)
    ui.set_editor_property("import_textures", False)
    ui.set_editor_property("import_rigid_mesh", False)
    ui.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_SKELETAL_MESH)
    ui.set_editor_property("original_import_type", unreal.FBXImportType.FBXIT_SKELETAL_MESH)
    ui.set_editor_property("automated_import_should_detect_type", False)
    ui.set_editor_property("skeleton", skeleton)
    ui.set_editor_property("create_physics_asset", False)
    ui.set_editor_property("override_full_name", True)
    content_type = _content_type(content_type_name)
    try:
        sm_data = ui.get_editor_property("skeletal_mesh_import_data")
        for prop, value in (
            ("import_morph_targets", False),
            ("update_skeleton_reference_pose", False),
            ("use_t0_as_ref_pose", True),
            ("preserve_smoothing_groups", True),
            ("import_meshes_in_bone_hierarchy", False),
            ("import_content_type", content_type),
            ("last_import_content_type", content_type),
        ):
            if value is None:
                continue
            try:
                sm_data.set_editor_property(prop, value)
            except Exception:
                pass
    except Exception as exc:
        log(f"WARN skeletal_mesh_import_data: {exc}")

    task = unreal.AssetImportTask()
    task.set_editor_property("filename", fbx_path.replace("\\", "/"))
    task.set_editor_property("destination_path", DEST_DIR)
    task.set_editor_property("destination_name", DEST_NAME)
    task.set_editor_property("automated", True)
    task.set_editor_property("save", True)
    task.set_editor_property("replace_existing", True)
    try:
        task.set_editor_property("async_", False)
    except Exception:
        pass
    task.set_editor_property("options", ui)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    paths = list(task.get_editor_property("imported_object_paths") or [])
    log(f"Imported ({content_type_name}) paths: {paths}")
    return paths


def import_cinched_fbx(fbx_path: str, skeleton) -> unreal.SkeletalMesh:
    """Reimport Blender geo onto a duplicate of the original MHC outfit.

    Binding the FBX directly to metahuman_base_skel fails Interchange bone-tree
    merge (Blender round-trip). Geometry-only reimport keeps the original skeleton.
    """
    duplicate_source_outfit()
    import_fbx_onto(fbx_path, skeleton, "Geometry")
    import_fbx_onto(fbx_path, skeleton, "SkinningWeights")
    mesh = unreal.load_asset(DEST_PATH)
    if isinstance(mesh, unreal.SkeletalMesh):
        return mesh
    raise RuntimeError("Cinched FBX import produced no skeletal mesh")


def copy_materials(src: unreal.SkeletalMesh, dst: unreal.SkeletalMesh, tank_mi) -> int:
    try:
        src_mats = list(src.get_editor_property("materials") or [])
        dst_mats = list(dst.get_editor_property("materials") or [])
    except Exception as exc:
        log(f"WARN materials: {exc}")
        return 0
    src_by_name = {}
    src_by_index = []
    for entry in src_mats:
        name = slot_name(entry)
        iface = mat_iface(entry)
        src_by_index.append((name, iface))
        if name:
            src_by_name[name.lower()] = iface
    changed = 0
    new_list = []
    for index, entry in enumerate(dst_mats):
        name = slot_name(entry)
        iface = None
        if name.lower() in src_by_name:
            iface = src_by_name[name.lower()]
        elif index < len(src_by_index):
            iface = src_by_index[index][1]
            if not name:
                try:
                    entry.set_editor_property("material_slot_name", src_by_index[index][0])
                except Exception:
                    pass
        if name.lower() == "tank" and tank_mi:
            iface = tank_mi
        if iface is not None:
            try:
                entry.set_editor_property("material_interface", iface)
                changed += 1
                log(f"  slot '{name or index}' -> {iface.get_name()}")
            except Exception as exc:
                log(f"  WARN slot '{name}': {exc}")
        new_list.append(entry)
    if changed:
        dst.set_editor_property("materials", new_list)
        unreal.EditorAssetLibrary.save_loaded_asset(dst)
    return changed


def mesh_name(comp) -> str:
    mesh = None
    try:
        mesh = comp.get_skeletal_mesh_asset()
    except Exception:
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
    try:
        if hasattr(comp, "set_cloth_max_distance_scale"):
            comp.set_cloth_max_distance_scale(0.0)
    except Exception:
        pass
    wiring.set_prop(comp, ["allow_cloth_actors", "b_allow_cloth_actors"], False)


OUTFIT_MESH_NAMES = (
    "MHC_CaptainGodfrey_Outfits",
    "SKM_Godfrey_Outfits_CinchedBoots",
)


def keep_tank_on_component(comp, tank_mi) -> None:
    if not tank_mi:
        return
    try:
        count = int(comp.get_num_materials())
    except Exception:
        return
    for index in range(count):
        mat = comp.get_material(index)
        if mat and mat.get_name() == "Tank":
            try:
                comp.set_material(index, tank_mi)
            except Exception:
                pass


def assign_on_blueprint(bp, mesh, tank_mi) -> int:
    assigned = 0
    for label, component, _h, _d in wiring.iter_all_components(bp):
        if "SkeletalMesh" not in component.get_class().get_name():
            continue
        if label in ("Body", "Face"):
            continue
        current = mesh_name(component)
        if current not in OUTFIT_MESH_NAMES:
            continue
        if assign_mesh(component, mesh):
            assigned += 1
            pin_cloth(component)
            keep_tank_on_component(component, tank_mi)
            log(f"  BP {label} ({current}) -> {mesh.get_name()}")
    return assigned


def assign_on_world(mesh, tank_mi) -> int:
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
        for comp in actor.get_components_by_class(unreal.SkeletalMeshComponent) or []:
            name = comp.get_name()
            if name in ("Body", "Face"):
                continue
            current = mesh_name(comp)
            if current not in OUTFIT_MESH_NAMES:
                continue
            if assign_mesh(comp, mesh):
                assigned += 1
                pin_cloth(comp)
                keep_tank_on_component(comp, tank_mi)
                log(f"  world {label}.{name} ({current}) -> {mesh.get_name()}")
    return assigned


def main() -> None:
    log("=== Apply cinched boots on Godfrey ===")
    fbx = os.path.join(project_root(), FBX_REL)
    if not os.path.isfile(fbx):
        raise RuntimeError(
            f"Missing {fbx} — export from Blender to this path first."
        )
    if fbx_has_leaf_end_bones(fbx):
        raise RuntimeError(
            "FBX still has Blender leaf bones (*_end). Those break metahuman_base_skel. "
            "In Blender: File > Export > FBX, turn Add Leaf Bones OFF, overwrite the same file, "
            "or run Scripts/blender_reexport_mh_outfit_fbx.py then re-run this script."
        )
    source = unreal.load_asset(SOURCE_OUTFIT)
    if not isinstance(source, unreal.SkeletalMesh):
        raise RuntimeError(f"Missing {SOURCE_OUTFIT}")
    skeleton = source_skeleton(source)
    if not skeleton:
        raise RuntimeError("Source outfit has no skeleton")
    log(f"Skeleton {skeleton.get_path_name()}")

    tank_mi = unreal.load_asset(MI_TANK)
    bp = unreal.load_asset(wiring.GODFREY_PERFORMER_BP)
    if not bp:
        raise RuntimeError(f"Missing {wiring.GODFREY_PERFORMER_BP}")
    restore_source_outfit(bp, source, tank_mi)
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)

    imported = import_cinched_fbx(fbx, skeleton)
    log(f"Imported {imported.get_path_name()}")
    copied = copy_materials(source, imported, tank_mi)
    log(f"Materials copied: {copied}")
    unreal.EditorAssetLibrary.save_loaded_asset(imported)

    bp = unreal.load_asset(wiring.GODFREY_PERFORMER_BP)
    if not bp:
        raise RuntimeError(f"Missing {wiring.GODFREY_PERFORMER_BP}")
    bp_hits = assign_on_blueprint(bp, imported, tank_mi)
    world_hits = assign_on_world(imported, tank_mi)
    log(f"Assigned BP={bp_hits} world={world_hits}")

    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    if not wiring.save_godfrey_performer_blueprint(bp):
        raise RuntimeError(wiring.BP_SAVE_LOCK_HINT)

    ok = bp_hits > 0 or world_hits > 0
    write_report(ok)
    if not ok:
        raise RuntimeError("Cinched mesh imported but was not assigned to Godfrey clothing")
    log(
        "PASS — original MHC_CaptainGodfrey_Outfits is unchanged. "
        "If collars are still open, re-run the Blender script with a smaller amount (e.g. 0.45) "
        "then re-run this apply script."
    )


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        unreal.log_error(f"[CinchedBoots] {exc}")
        write_report(False)
        raise
