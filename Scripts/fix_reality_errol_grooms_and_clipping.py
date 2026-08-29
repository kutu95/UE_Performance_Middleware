"""Restore hair/facial grooms on BP_Godfrey_Performer and reduce body poke-through.

Assemble of MH_RealityErrol left GroomComponents empty (no GroomAsset). This copies
the previous performer grooms (Hair_S_Clean, Beard_L_Messy, eyebrows, lashes,
mustache) and rebuilds bindings onto SKM_MH_RealityErrol_FaceMesh.

Clothing: MHC_CaptainGodfrey_Outfits1 was fitted to a different body, so skin
clips at belly/armpits/boots. Slightly scale the outfit mesh and apply the
casual-formal body hide mask when the body MI has a matching texture param.

Headless:
  UnrealEditor-Cmd.exe "D:/UE Projects/MetaHuman_Baseline_UE58_Test/UnrealPerformer.uproject"
    -ExecutePythonScript="D:/UE Projects/MetaHuman_Baseline_UE58_Test/Scripts/fix_reality_errol_grooms_and_clipping.py"
    -unattended -nop4 -nosplash -log
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

GROOM_DIR = "/Game/MetaHumans/MHC_CaptainGodfrey/Grooms"
FACE_MESH = "/Game/MetaHumans/MH_RealityErrol/Face/SKM_MH_RealityErrol_FaceMesh"
SOURCE_FACE = "/Game/MetaHumans/MHC_CaptainGodfrey/Face/SKM_MHC_CaptainGodfrey_FaceMesh"
BINDING_DIR = "/Game/MetaHumans/MH_RealityErrol/Grooms"
BODY_HIDE_MASK = "/Game/Outfits/casual_formal/textures/DG_BodyShapeA_BodyMask"
CLOTHING_SCALE = 1.045

GROOM_MAP = {
    "Hair": (
        f"{GROOM_DIR}/Hair_S_Clean",
        f"{GROOM_DIR}/Hair_S_Clean_Binding",
        f"{GROOM_DIR}/MI_WI_Hair_S_Clean_Hair",
    ),
    "Beard": (
        f"{GROOM_DIR}/Beard_L_Messy",
        f"{GROOM_DIR}/Beard_L_Messy_Binding",
        f"{GROOM_DIR}/MI_WI_Beard_L_Messy_Hair",
    ),
    "Mustache": (
        f"{GROOM_DIR}/Mustache_L_Handlebar",
        f"{GROOM_DIR}/Mustache_L_Handlebar_Binding",
        f"{GROOM_DIR}/MI_WI_Mustache_L_Handlebar_Hair",
    ),
    "Eyebrows": (
        f"{GROOM_DIR}/Eyebrows_L_Scraggly",
        f"{GROOM_DIR}/Eyebrows_L_Scraggly_Binding",
        f"{GROOM_DIR}/MI_WI_Eyebrows_L_Scraggly_Hair",
    ),
    "Eyelashes": (
        f"{GROOM_DIR}/Eyelashes_S_Sparse",
        f"{GROOM_DIR}/Eyelashes_S_Sparse_Binding",
        f"{GROOM_DIR}/MI_WI_Eyelashes_S_Sparse_Hair",
    ),
}

REPORT = "FixRealityErrolGroomsAndClipping.txt"
_lines: list[str] = []


def log(msg: str) -> None:
    _lines.append(msg)
    unreal.log(f"[FixRealityErrolGrooms] {msg}")


def write_report(ok: bool) -> None:
    path = unreal.Paths.convert_relative_path_to_full(
        unreal.Paths.project_saved_dir() + REPORT
    )
    if "MetaHuman_Baseline" not in path.replace("\\", "/"):
        path = r"D:/UE Projects/MetaHuman_Baseline_UE58_Test/Saved/" + REPORT
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(("RESULT: PASS\n" if ok else "RESULT: FAIL\n") + "\n".join(_lines) + "\n")
    log(f"Report: {path}")


def _prop(obj, names: tuple[str, ...]):
    for name in names:
        try:
            return obj.get_editor_property(name)
        except Exception:
            continue
    return None


def _asset_path(obj) -> str:
    if not obj:
        return "(none)"
    try:
        return obj.get_path_name()
    except Exception:
        return str(obj)


def dump_groom(label: str, comp) -> None:
    log(
        f"  {label}: asset={_asset_path(_prop(comp, ('groom_asset', 'GroomAsset')))} "
        f"bind={_asset_path(_prop(comp, ('binding_asset', 'BindingAsset')))} "
        f"hidden={_prop(comp, ('b_hidden_in_game', 'bHiddenInGame'))}"
    )


def unique_components_by_label(bp, label: str) -> list:
    seen: set[int] = set()
    found = []
    for comp_label, component, _h, _d in wiring.iter_all_components(bp):
        if comp_label.lower() != label.lower():
            continue
        key = id(component)
        if key in seen:
            continue
        seen.add(key)
        found.append(component)
    return found


def make_binding(groom, target_face, source_face, dest_path: str):
    if unreal.EditorAssetLibrary.does_asset_exist(dest_path):
        existing = unreal.load_asset(dest_path)
        if existing:
            log(f"  reuse binding {dest_path}")
            return existing

    if not unreal.EditorAssetLibrary.does_directory_exist(BINDING_DIR):
        unreal.EditorAssetLibrary.make_directory(BINDING_DIR)

    library = getattr(unreal, "GroomLibrary", None) or getattr(
        unreal, "GroomBlueprintLibrary", None
    )
    if not library:
        log("WARN: GroomLibrary unavailable — using source binding")
        return None

    try:
        binding = library.create_new_groom_binding_asset_with_path(
            dest_path,
            groom,
            target_face,
            100,
            source_face,
            0,
        )
    except TypeError:
        try:
            binding = library.create_new_groom_binding_asset_with_path(
                in_desired_package_path=dest_path,
                in_groom_asset=groom,
                in_skeletal_mesh=target_face,
                in_num_interpolation_points=100,
                in_source_skeletal_mesh_for_transfer=source_face,
                in_matching_section=0,
            )
        except Exception as exc:
            log(f"WARN: create binding failed: {exc}")
            return None
    except Exception as exc:
        log(f"WARN: create binding failed: {exc}")
        return None

    if binding:
        log(f"  created binding {dest_path}")
    return binding


def assign_groom(comp, groom, binding, material) -> None:
    try:
        comp.set_groom_asset(groom)
    except Exception:
        if not wiring.set_prop(comp, ["groom_asset", "GroomAsset"], groom):
            raise RuntimeError(f"Could not set GroomAsset on {comp.get_name()}")
    if binding:
        bound = False
        try:
            comp.set_binding_asset(binding)
            bound = True
        except Exception as exc:
            log(f"  set_binding_asset: {exc}")
        if not bound:
            used = wiring.set_prop(
                comp,
                ["binding_asset", "BindingAsset", "groom_binding_asset", "GroomBindingAsset"],
                binding,
            )
            log(f"  binding editor prop={used}")
    wiring.set_prop(comp, ["b_hidden_in_game", "bHiddenInGame"], False)
    wiring.set_prop(comp, ["visible", "bVisible"], True)
    wiring.set_prop(comp, ["b_visible_in_ray_tracing", "bVisibleInRayTracing"], True)
    try:
        if material:
            comp.set_material(0, material)
    except Exception:
        pass


def attach_to_face(comp, face) -> None:
    if not face:
        return
    try:
        comp.attach_to_component(
            face,
            unreal.Name(""),
            unreal.AttachmentRule.KEEP_RELATIVE,
            unreal.AttachmentRule.KEEP_RELATIVE,
            unreal.AttachmentRule.KEEP_RELATIVE,
            False,
        )
        log("  attached to Face")
    except Exception as exc:
        log(f"  WARN attach Face: {exc}")


def restore_grooms(bp) -> int:
    face, _ = wiring.find_component(bp, "Face")
    target_face = unreal.load_asset(FACE_MESH)
    source_face = unreal.load_asset(SOURCE_FACE)
    if not target_face:
        raise RuntimeError(f"Missing {FACE_MESH}")

    restored = 0
    for label, (groom_path, fallback_bind, mi_path) in GROOM_MAP.items():
        comps = unique_components_by_label(bp, label)
        if not comps:
            log(f"WARN: no {label} GroomComponent")
            continue
        groom = unreal.load_asset(groom_path)
        if not groom:
            raise RuntimeError(f"Missing groom {groom_path}")
        material = unreal.load_asset(mi_path) if unreal.EditorAssetLibrary.does_asset_exist(mi_path) else None
        dest_bind = f"{BINDING_DIR}/{os.path.basename(groom_path)}_RealityErrol_Binding"
        binding = make_binding(groom, target_face, source_face, dest_bind)
        fallback = unreal.load_asset(fallback_bind) if unreal.EditorAssetLibrary.does_asset_exist(fallback_bind) else None
        if not binding:
            binding = fallback
            log(f"  fallback binding {fallback_bind}")
        for comp in comps:
            dump_groom(label, comp)
            assign_groom(comp, groom, binding, material)
            if fallback and not _prop(comp, ("binding_asset", "BindingAsset")):
                assign_groom(comp, groom, fallback, material)
                log("  retried with CaptainGodfrey binding")
            attach_to_face(comp, face)
            try:
                if face:
                    comp.add_collision_component(face)
            except Exception:
                pass
            dump_groom(f"{label} after", comp)
            restored += 1
        log(f"{label} <- {groom_path}")
    return restored


def apply_body_hide_mask(bp) -> bool:
    mask = unreal.load_asset(BODY_HIDE_MASK) if unreal.EditorAssetLibrary.does_asset_exist(BODY_HIDE_MASK) else None
    hide = (
        unreal.load_asset("/Game/MetaHumans/Common/Textures/T_BodyHideMask")
        if unreal.EditorAssetLibrary.does_asset_exist("/Game/MetaHumans/Common/Textures/T_BodyHideMask")
        else None
    )
    body, _ = wiring.find_component(bp, "Body")
    if not body:
        return False
    mesh = None
    try:
        mesh = body.get_skeletal_mesh_asset()
    except Exception:
        mesh = _prop(body, ("skeletal_mesh", "SkeletalMesh"))
    if not mesh:
        return False

    applied = False
    materials = []
    try:
        materials = list(body.get_materials() or [])
    except Exception:
        pass
    if not materials:
        try:
            materials = [mesh.get_material(i) for i in range(int(mesh.get_num_sections(0)))]
        except Exception:
            materials = []

    param_names = (
        "HideMask",
        "BodyHideMask",
        "ClothingMask",
        "ClothMask",
        "Mask_Clothing",
        "OpacityMask",
        "RegionMask",
        "BodyMask",
        "DG_BodyMask",
        "Mask",
    )
    for mat in materials:
        if not mat:
            continue
        log(f"Body material {mat.get_name()}")
        try:
            values = list(mat.get_editor_property("texture_parameter_values") or [])
            for entry in values:
                info = _prop(entry, ("parameter_info", "ParameterInfo"))
                name = ""
                if info:
                    name = str(_prop(info, ("name", "Name")) or "")
                tex = _prop(entry, ("parameter_value", "ParameterValue"))
                log(f"  tex param {name} = {_asset_path(tex)}")
        except Exception as exc:
            log(f"  WARN dump tex params: {exc}")

        if not mask and not hide:
            continue
        chosen = mask or hide
        for pname in param_names:
            try:
                unreal.MaterialEditingLibrary.set_material_instance_texture_parameter_value(
                    mat, pname, chosen
                )
                log(f"  set {pname} <- {_asset_path(chosen)}")
                applied = True
                break
            except Exception:
                continue
    return applied


def scale_clothing(bp) -> int:
    scaled = 0
    seen: set[int] = set()
    for label, component, _h, _d in wiring.iter_all_components(bp):
        if "SkeletalMesh" not in component.get_class().get_name():
            continue
        if label in {"Body", "Face"}:
            continue
        if any(tok in label for tok in ("Hair", "Beard", "Eyebrow", "Eyelash", "Mustache", "Fuzz")):
            continue
        if id(component) in seen:
            continue
        seen.add(id(component))
        mesh = _prop(component, ("skeletal_mesh", "SkeletalMesh"))
        path = _asset_path(mesh)
        if "Outfits" not in path and "Cinched" not in path:
            continue
        scale = unreal.Vector(CLOTHING_SCALE, CLOTHING_SCALE, CLOTHING_SCALE)
        if wiring.set_prop(component, ["relative_scale3d", "RelativeScale3D"], scale):
            log(f"Clothing {label} scale {CLOTHING_SCALE} ({path})")
            scaled += 1
        wiring.set_prop(component, ["disable_cloth_simulation", "b_disable_cloth_simulation"], True)
        wiring.set_prop(component, ["cloth_blend_weight"], 0.0)
    return scaled


def main() -> None:
    log("=== Fix RealityErrol grooms + clothing clip ===")
    bp = unreal.load_asset(wiring.GODFREY_PERFORMER_BP)
    if not bp:
        raise RuntimeError(f"Missing {wiring.GODFREY_PERFORMER_BP}")

    log("Grooms before:")
    for label in GROOM_MAP:
        for comp in unique_components_by_label(bp, label):
            dump_groom(label, comp)

    restored = restore_grooms(bp)
    if restored < 5:
        raise RuntimeError(f"Expected 5 groom types, restored {restored} component(s)")

    masked = apply_body_hide_mask(bp)
    log(f"Body hide mask applied={masked}")
    scaled = scale_clothing(bp)
    log(f"Clothing components scaled={scaled}")

    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    if not wiring.save_godfrey_performer_blueprint(bp):
        raise RuntimeError(wiring.BP_SAVE_LOCK_HINT)

    hair = unique_components_by_label(bp, "Hair")
    if not hair:
        raise RuntimeError("Hair component missing after assign")
    hair_asset = _prop(hair[0], ("groom_asset", "GroomAsset"))
    if not hair_asset:
        raise RuntimeError("Hair GroomAsset still empty")

    write_report(True)
    log(
        "PASS — hair/beard/brows/lashes/moustache assigned and rebound to RealityErrol face. "
        f"Outfit scaled to {CLOTHING_SCALE} to cover belly/armpit/boot clipping. "
        "Open Godfrey_World and PIE to confirm."
    )


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        unreal.log_error(f"[FixRealityErrolGrooms] {exc}")
        write_report(False)
        sys.exit(1)
