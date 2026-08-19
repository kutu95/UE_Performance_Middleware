"""Make Godfrey's Casual Formal tank a solid, lighter cotton (UE 5.8).

Does not rebuild the outfit. Coat and pants stay on WI_OA_Casual_formal.
Creates MI_Tank_CottonSolid + cotton albedo/ORM, then retargets every Tank
material slot that the purchased outfit uses.

Headless:
  Saved/run_fix_godfrey_tank_cotton.bat
or Tools → Execute Python Script → this file (close the editor first if headless).
"""
from __future__ import annotations

import os
import random
import struct
import sys
import zlib

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

import unreal

try:
    import godfrey_blueprint_wiring as wiring
except Exception:
    wiring = None

PARENT_TANK = "/Game/Outfits/casual_formal/Materials/Cloth/Tank"
MI_PATH = "/Game/Outfits/casual_formal/Materials/Cloth/MI_Tank_CottonSolid"
MI_NAME = "MI_Tank_CottonSolid"
MI_DIR = "/Game/Outfits/casual_formal/Materials/Cloth"
TEX_DIR = "/Game/Outfits/casual_formal/textures"
DIFFUSE_NAME = "Tank_Cotton_Diffuse"
ORM_NAME = "Tank_Cotton_ORM"
DIFFUSE_PATH = f"{TEX_DIR}/{DIFFUSE_NAME}"
ORM_PATH = f"{TEX_DIR}/{ORM_NAME}"

# Light warm cotton (sRGB-ish). Knit holes are killed by replacing DiffuseColorMap
# and dropping map-weight / subsurface.
COTTON_SRGB = (214, 204, 188)
COTTON_LINEAR = unreal.LinearColor(0.78, 0.74, 0.66, 1.0)
SUBSURFACE_OFF = unreal.LinearColor(0.0, 0.0, 0.0, 1.0)

SCALAR_OVERRIDES = {
    "DiffuseColorMapWeight": 0.35,
    "Opacity": 1.0,
    "OpacityMask": 1.0,
    "Metallic": 0.0,
    "Roughness": 0.78,
    "Specular": 0.22,
    "AmbientOcclusion": 1.0,
    "SurfaceThickness": 2.0,
    "ClearCoat": 0.0,
    "ClearCoatRoughness": 0.9,
    "Shininess": 0.05,
}
VECTOR_OVERRIDES = {
    "DiffuseColor": COTTON_LINEAR,
    "BaseColor": COTTON_LINEAR,
    "SubsurfaceColor": SUBSURFACE_OFF,
}

KNOWN_TANK_ASSETS = (
    "/Game/Outfits/casual_formal/Meshes/A/SKM_astank_4690_shape",
    "/Game/Outfits/casual_formal/Meshes/B/SKM_astank_shape",
    "/Game/Outfits/casual_formal/Meshes/SM_body_a_CombinedSkelMesh_body_a_CombinedSkelMesh",
    "/Game/Outfits/casual_formal/Meshes/SM_body_b_CombinedSkelMesh_body_b_CombinedSkelMesh",
    "/Game/Outfits/casual_formal/ClothAssets/CA_a",
    "/Game/Outfits/casual_formal/ClothAssets/CA_b",
    "/Game/Outfits/OA_Casual_formal",
    "/Game/MetaHumans/MHC_CaptainGodfrey/Clothing/MHC_CaptainGodfrey_Outfits",
    "/Game/MetaHumans/MHC_Errol/Clothing/MHC_Errol_Outfits",
)

REPORT = "FixGodfreyTankCotton.txt"
_lines: list[str] = []


def log(msg: str) -> None:
    _lines.append(msg)
    unreal.log(f"[TankCotton] {msg}")


def saved_path(filename: str) -> str:
    return unreal.Paths.convert_relative_path_to_full(
        unreal.Paths.project_saved_dir() + filename
    )


def png_chunk(tag: bytes, data: bytes) -> bytes:
    crc = zlib.crc32(tag + data) & 0xFFFFFFFF
    return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", crc)


def write_png_rgb(path: str, width: int, height: int, rgb_rows) -> None:
    raw = b"".join(b"\x00" + row for row in rgb_rows)
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    payload = (
        b"\x89PNG\r\n\x1a\n"
        + png_chunk(b"IHDR", ihdr)
        + png_chunk(b"IDAT", zlib.compress(raw, 9))
        + png_chunk(b"IEND", b"")
    )
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as handle:
        handle.write(payload)


def make_cotton_pngs() -> tuple[str, str]:
    source_dir = os.path.join(
        unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()),
        "CostumeSource",
    )
    os.makedirs(source_dir, exist_ok=True)
    diffuse_png = os.path.join(source_dir, "Tank_Cotton_Diffuse.png")
    orm_png = os.path.join(source_dir, "Tank_Cotton_ORM.png")
    size = 512
    rng = random.Random(42)
    base_r, base_g, base_b = COTTON_SRGB
    diff_rows = []
    orm_rows = []
    for _y in range(size):
        d = bytearray()
        o = bytearray()
        for _x in range(size):
            n = rng.randint(-6, 6)
            d.extend(
                (
                    max(0, min(255, base_r + n)),
                    max(0, min(255, base_g + n)),
                    max(0, min(255, base_b + n)),
                )
            )
            # AO, Roughness, Metallic — solid cotton, no knit holes in alpha.
            o.extend((235, 198, 8))
        diff_rows.append(bytes(d))
        orm_rows.append(bytes(o))
    write_png_rgb(diffuse_png, size, size, diff_rows)
    write_png_rgb(orm_png, size, size, orm_rows)
    log(f"Wrote {diffuse_png}")
    log(f"Wrote {orm_png}")
    return diffuse_png, orm_png


def import_texture(png_path: str, dest_dir: str, asset_name: str, *, srgb: bool) -> unreal.Texture2D:
    if not unreal.EditorAssetLibrary.does_directory_exist(dest_dir):
        unreal.EditorAssetLibrary.make_directory(dest_dir)
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", png_path.replace("\\", "/"))
    task.set_editor_property("destination_path", dest_dir)
    task.set_editor_property("destination_name", asset_name)
    task.set_editor_property("automated", True)
    task.set_editor_property("save", True)
    task.set_editor_property("replace_existing", True)
    try:
        task.set_editor_property("async_", False)
    except Exception:
        pass
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    imported = list(task.get_editor_property("imported_object_paths") or [])
    log(f"Import {asset_name} -> {imported}")
    asset = unreal.EditorAssetLibrary.load_asset(f"{dest_dir}/{asset_name}")
    if not isinstance(asset, unreal.Texture2D):
        for path in imported:
            loaded = unreal.load_asset(path.split(".", 1)[0])
            if isinstance(loaded, unreal.Texture2D):
                asset = loaded
                break
    if not isinstance(asset, unreal.Texture2D):
        raise RuntimeError(f"Failed to import texture {asset_name}")
    try:
        asset.set_editor_property("srgb", srgb)
    except Exception as exc:
        log(f"WARN: srgb on {asset_name}: {exc}")
    try:
        asset.set_editor_property(
            "compression_settings",
            unreal.TextureCompressionSettings.TC_DEFAULT if srgb else unreal.TextureCompressionSettings.TC_MASKS,
        )
    except Exception as exc:
        log(f"WARN: compression on {asset_name}: {exc}")
    unreal.EditorAssetLibrary.save_loaded_asset(asset)
    return asset


def dump_material(mat) -> None:
    log(f"=== Material {mat.get_path_name()} class={mat.get_class().get_name()} ===")
    for prop in ("blend_mode", "shading_model", "two_sided", "b_used_with_skeletal_mesh", "b_used_with_clothing"):
        try:
            log(f"  {prop}={mat.get_editor_property(prop)}")
        except Exception:
            pass
    lib = unreal.MaterialEditingLibrary
    for getter, kind in (
        ("get_scalar_parameter_names", "scalar"),
        ("get_vector_parameter_names", "vector"),
        ("get_texture_parameter_names", "texture"),
        ("get_static_switch_parameter_names", "switch"),
    ):
        fn = getattr(lib, getter, None)
        if not fn:
            continue
        try:
            names = list(fn(mat) or [])
        except Exception as exc:
            log(f"  {kind} names: {exc}")
            continue
        log(f"  {kind} params: {[str(n) for n in names]}")
        for name in names:
            try:
                if kind == "scalar":
                    log(f"    {name}={lib.get_material_default_scalar_parameter_value(mat, name)}")
                elif kind == "vector":
                    log(f"    {name}={lib.get_material_default_vector_parameter_value(mat, name)}")
                elif kind == "texture":
                    tex = lib.get_material_default_texture_parameter_value(mat, name)
                    log(f"    {name}={tex.get_path_name() if tex else None}")
                elif kind == "switch":
                    log(f"    {name}={lib.get_material_default_static_switch_parameter_value(mat, name)}")
            except Exception as exc:
                log(f"    {name}: {exc}")


def ensure_mic(parent, diffuse, orm) -> unreal.MaterialInstanceConstant:
    if unreal.EditorAssetLibrary.does_asset_exist(MI_PATH):
        mic = unreal.EditorAssetLibrary.load_asset(MI_PATH)
        log(f"Loaded existing {MI_PATH}")
    else:
        factory = unreal.MaterialInstanceConstantFactoryNew()
        mic = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            MI_NAME, MI_DIR, unreal.MaterialInstanceConstant, factory
        )
        if not mic:
            raise RuntimeError(f"Failed to create {MI_PATH}")
        log(f"Created {MI_PATH}")
    mic.set_editor_property("parent", parent)

    overrides = unreal.MaterialInstanceBasePropertyOverrides()
    for names, value in (
        (("override_blend_mode", "b_override_blend_mode"), True),
        (("blend_mode",), unreal.BlendMode.BLEND_OPAQUE),
        (("override_two_sided", "b_override_two_sided"), True),
        (("two_sided",), True),
        (("override_shading_model", "b_override_shading_model"), True),
        (("shading_model",), unreal.MaterialShadingModel.MSM_DEFAULT_LIT),
        (("override_opacity_mask_clip_value", "b_override_opacity_mask_clip_value"), True),
        (("opacity_mask_clip_value",), 0.33),
    ):
        for name in names if isinstance(names, tuple) else (names,):
            try:
                overrides.set_editor_property(name, value)
                log(f"base override {name}={value}")
                break
            except Exception:
                continue
    try:
        mic.set_editor_property("base_property_overrides", overrides)
    except Exception as exc:
        log(f"WARN: base_property_overrides: {exc}")

    lib = unreal.MaterialEditingLibrary
    for name, value in SCALAR_OVERRIDES.items():
        try:
            lib.set_material_instance_scalar_parameter_value(mic, unreal.Name(name), value)
            log(f"scalar {name}={value}")
        except Exception as exc:
            log(f"WARN scalar {name}: {exc}")
    for name, value in VECTOR_OVERRIDES.items():
        try:
            lib.set_material_instance_vector_parameter_value(mic, unreal.Name(name), value)
            log(f"vector {name}={value}")
        except Exception as exc:
            log(f"WARN vector {name}: {exc}")

    texture_slots = {
        "DiffuseColorMap": diffuse,
        "BaseColor": diffuse,
        "Diffuse": diffuse,
        "ORM": orm,
        "PackedORM": orm,
        "Metallic": orm,
        "Roughness": orm,
        "AmbientOcclusion": orm,
    }
    for name, tex in texture_slots.items():
        try:
            lib.set_material_instance_texture_parameter_value(mic, unreal.Name(name), tex)
            log(f"texture {name}={tex.get_name()}")
        except Exception:
            pass

    lib.update_material_instance(mic)
    unreal.EditorAssetLibrary.save_loaded_asset(mic)
    return mic


def is_tank_material(mat) -> bool:
    if not mat:
        return False
    path = mat.get_path_name()
    name = mat.get_name()
    if name in ("Tank", MI_NAME):
        return True
    if PARENT_TANK in path or MI_PATH in path:
        return True
    return False


def replace_on_skeletal_mesh(mesh: unreal.SkeletalMesh, mic) -> int:
    changed = 0
    try:
        mats = list(mesh.get_editor_property("materials") or [])
    except Exception:
        return 0
    new_mats = []
    for entry in mats:
        try:
            iface = entry.get_editor_property("material_interface")
            slot = str(entry.get_editor_property("material_slot_name"))
        except Exception:
            new_mats.append(entry)
            continue
        if is_tank_material(iface) or slot.lower() in ("tank", "shirt"):
            try:
                entry.set_editor_property("material_interface", mic)
                changed += 1
                log(f"  SKM {mesh.get_name()} slot '{slot}' -> {MI_NAME}")
            except Exception as exc:
                log(f"  WARN set slot '{slot}' on {mesh.get_name()}: {exc}")
        new_mats.append(entry)
    if changed:
        try:
            mesh.set_editor_property("materials", new_mats)
        except Exception as exc:
            log(f"  WARN write materials on {mesh.get_name()}: {exc}")
            return 0
        unreal.EditorAssetLibrary.save_loaded_asset(mesh)
    return changed


def replace_generic_materials_array(obj, mic, label: str) -> int:
    changed = 0
    for prop in ("materials", "override_materials", "material_slots"):
        try:
            mats = obj.get_editor_property(prop)
        except Exception:
            continue
        if mats is None:
            continue
        try:
            items = list(mats)
        except Exception:
            continue
        new_items = []
        mutated = False
        for item in items:
            mat = item
            slot = ""
            if hasattr(item, "get_editor_property"):
                try:
                    mat = item.get_editor_property("material_interface")
                except Exception:
                    try:
                        mat = item.get_editor_property("material")
                    except Exception:
                        mat = item
                try:
                    slot = str(item.get_editor_property("material_slot_name"))
                except Exception:
                    slot = ""
            if is_tank_material(mat) or slot.lower() in ("tank", "shirt"):
                if hasattr(item, "set_editor_property"):
                    for field in ("material_interface", "material"):
                        try:
                            item.set_editor_property(field, mic)
                            mutated = True
                            changed += 1
                            log(f"  {label}.{prop} '{slot}' -> {MI_NAME}")
                            break
                        except Exception:
                            continue
                else:
                    item = mic
                    mutated = True
                    changed += 1
                    log(f"  {label}.{prop} -> {MI_NAME}")
            new_items.append(item)
        if mutated:
            try:
                obj.set_editor_property(prop, new_items)
                unreal.EditorAssetLibrary.save_loaded_asset(obj)
            except Exception as exc:
                log(f"  WARN write {label}.{prop}: {exc}")
    return changed


def collect_targets() -> list:
    paths = set(KNOWN_TANK_ASSETS)
    try:
        refs = unreal.EditorAssetLibrary.find_package_referencers(PARENT_TANK, True) or []
        for ref in refs:
            paths.add(str(ref).split(".", 1)[0])
        log(f"Referencers of Tank: {len(refs)}")
    except Exception as exc:
        log(f"WARN find_package_referencers: {exc}")
    loaded = []
    for path in sorted(paths):
        if not unreal.EditorAssetLibrary.does_asset_exist(path):
            continue
        asset = unreal.EditorAssetLibrary.load_asset(path)
        if asset:
            loaded.append(asset)
            log(f"Target {path} ({asset.get_class().get_name()})")
    return loaded


def apply_to_performer_blueprint(mic) -> int:
    if wiring is None:
        log("WARN: godfrey_blueprint_wiring unavailable — skip BP")
        return 0
    bp = unreal.load_asset(wiring.GODFREY_PERFORMER_BP)
    if not bp:
        log(f"WARN: missing {wiring.GODFREY_PERFORMER_BP}")
        return 0
    changed = 0
    clothing_labels = (
        "Torso",
        "Legs",
        "Feet",
        "SkeletalMesh",
        "SkeletalMesh1",
        "SkeletalMesh2",
    )
    for label, component, _h, _d in wiring.iter_all_components(bp):
        if "SkeletalMesh" not in component.get_class().get_name():
            continue
        if label not in clothing_labels and "Torso" not in label:
            continue
        try:
            mesh = component.get_editor_property("skeletal_mesh")
        except Exception:
            mesh = None
        num_slots = 0
        try:
            num_slots = int(component.get_num_materials())
        except Exception:
            if mesh:
                try:
                    num_slots = len(list(mesh.get_editor_property("materials") or []))
                except Exception:
                    num_slots = 0
        for index in range(num_slots):
            try:
                current = component.get_material(index)
            except Exception:
                current = None
            slot_name = ""
            if mesh:
                try:
                    mats = list(mesh.get_editor_property("materials") or [])
                    if index < len(mats):
                        slot_name = str(mats[index].get_editor_property("material_slot_name"))
                except Exception:
                    pass
            if is_tank_material(current) or slot_name.lower() in ("tank", "shirt"):
                try:
                    component.set_material(index, mic)
                    changed += 1
                    log(f"  BP {label}[{index}] '{slot_name}' -> {MI_NAME}")
                except Exception as exc:
                    log(f"  WARN BP {label}[{index}]: {exc}")
    if changed:
        try:
            unreal.BlueprintEditorLibrary.compile_blueprint(bp)
        except Exception as exc:
            log(f"WARN compile performer: {exc}")
        if hasattr(wiring, "save_godfrey_performer_blueprint"):
            wiring.save_godfrey_performer_blueprint(bp)
        else:
            unreal.EditorAssetLibrary.save_loaded_asset(bp)
    return changed


def apply_to_world_actor(mic) -> int:
    changed = 0
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
        if "Godfrey" not in label and "MHC_Errol" not in label and "CaptainGodfrey" not in label:
            continue
        meshes = actor.get_components_by_class(unreal.SkeletalMeshComponent) or []
        for comp in meshes:
            name = comp.get_name()
            try:
                count = int(comp.get_num_materials())
            except Exception:
                continue
            for index in range(count):
                current = comp.get_material(index)
                if is_tank_material(current):
                    try:
                        comp.set_material(index, mic)
                        changed += 1
                        log(f"  Actor {label}.{name}[{index}] -> {MI_NAME}")
                    except Exception as exc:
                        log(f"  WARN actor slot: {exc}")
    return changed


def write_report(ok: bool) -> None:
    path = saved_path(REPORT)
    header = "RESULT: PASS\n" if ok else "RESULT: FAIL\n"
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(header + "\n".join(_lines) + "\n")
    log(f"Report: {path}")


def main() -> None:
    log("=== Solid cotton tank on Casual Formal (coat/pants unchanged) ===")
    parent = unreal.EditorAssetLibrary.load_asset(PARENT_TANK)
    if not parent:
        raise RuntimeError(f"Missing {PARENT_TANK}")
    dump_material(parent)

    diffuse_png, orm_png = make_cotton_pngs()
    diffuse = import_texture(diffuse_png, TEX_DIR, DIFFUSE_NAME, srgb=True)
    orm = import_texture(orm_png, TEX_DIR, ORM_NAME, srgb=False)
    mic = ensure_mic(parent, diffuse, orm)
    dump_material(mic)

    changed = 0
    for asset in collect_targets():
        if isinstance(asset, unreal.SkeletalMesh):
            changed += replace_on_skeletal_mesh(asset, mic)
        changed += replace_generic_materials_array(asset, mic, asset.get_name())
    changed += apply_to_performer_blueprint(mic)
    changed += apply_to_world_actor(mic)

    ok = changed > 0
    log(f"Tank slots retargeted: {changed}")
    if not ok:
        log("No Tank slots were rewritten — check MHC outfits in the editor.")
    write_report(ok)
    if not ok:
        raise RuntimeError("No Tank material slots updated")
    log("PASS — PIE Godfrey: tank should read as solid light cotton under the coat.")


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        unreal.log_error(f"[TankCotton] {exc}")
        write_report(False)
        raise
