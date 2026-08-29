"""Use MH_RealityErrol's own grooms on BP_Godfrey_Performer and strip the transferred costume.

Does NOT use MHC_Errol / CaptainGodfrey grooms.
Hair/beard/moustache/brows/lashes come from MetaHumanCharacter plugin assets
referenced by MH_RealityErrol (Hair_S_Casual, Beard_S_Uneven, Mustache_L_Full, …).

Also patches the placed Godfrey_World actor. Instance overrides from the old
CaptainGodfrey grooms (Hair_S_Clean / Beard_L_Messy / Mustache_L_Handlebar)
otherwise keep showing in PIE even after the Blueprint CDO is correct.

Headless (editor closed):
  UnrealEditor-Cmd.exe … UnrealPerformer.uproject /Game/Godfrey_World
    -ExecutePythonScript=…/Scripts/fix_reality_errol_native_grooms.py

Editor: Tools → Execute Python Script → this file (stop PIE).
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

CHAR_PATH = "/Game/Actors/RealityErrol/MH_RealityErrol"
DONOR_BP = "/Game/MetaHumans/MH_RealityErrol/BP_MH_RealityErrol"
BODY_MESH = "/Game/MetaHumans/MH_RealityErrol/Body/SKM_MH_RealityErrol_BodyMesh"
FACE_MESH = "/Game/MetaHumans/MH_RealityErrol/Face/SKM_MH_RealityErrol_FaceMesh"
BINDING_DIR = "/Game/MetaHumans/MH_RealityErrol/Grooms"
PLUGIN = "/MetaHumanCharacter/Optional/Grooms"
PLUGIN_HAIR_MI = "/MetaHumanCharacter/Materials/MI_Hair"
PLUGIN_CARDS_MI = "/MetaHumanCharacter/Materials/MI_Hair_Cards"
PLUGIN_HELMET_MI = "/MetaHumanCharacter/Materials/MI_Hair_Helmet"
PLUGIN_FACIAL_MI = "/MetaHumanCharacter/Materials/MI_Facial_Hair"
MI_DIR = "/Game/MetaHumans/MH_RealityErrol/Grooms"
LEVEL_PATH = "/Game/Godfrey_World"
BANNED_GROOMS = (
    "Hair_S_Clean",
    "Beard_L_Messy",
    "Mustache_L_Handlebar",
    "Eyelashes_S_Sparse",
    "MI_WI_Hair_S_Clean",
    "MI_WI_Beard_L_Messy",
    "MI_WI_Mustache_L_Handlebar",
    "MI_WI_Eyelashes_S_Sparse",
)

# Worn on MH_RealityErrol (wardrobe WI_* + screenshot). Not Hair_S_Clean / Beard_L_Messy.
GROOM_MAP = {
    "Hair": (
        f"{PLUGIN}/GroomAssets/Hair/Hair_S_Casual/Hair_S_Casual",
        f"{PLUGIN}/Bindings/Hair/Hair_S_Casual_Binding",
    ),
    "Beard": (
        f"{PLUGIN}/GroomAssets/Beards/Beard_S_Uneven/Beard_S_Uneven",
        f"{PLUGIN}/Bindings/Beards/Beard_S_Uneven_Binding",
    ),
    "Mustache": (
        f"{PLUGIN}/GroomAssets/Mustaches/Mustache_L_Full/Mustache_L_Full",
        f"{PLUGIN}/Bindings/Mustaches/Mustache_L_Full_Binding",
    ),
    "Eyebrows": (
        f"{PLUGIN}/GroomAssets/Eyebrows/Eyebrows_M_Scraggly/Eyebrows_L_Scraggly",
        f"{PLUGIN}/Bindings/Eyebrows/Eyebrows_L_Scraggly_Binding",
    ),
    "Eyelashes": (
        f"{PLUGIN}/GroomAssets/Eyelashes/Eyelashes_S_Fine/Eyelashes_S_Fine",
        f"{PLUGIN}/Bindings/Eyelashes/Eyelashes_S_Fine_Binding",
    ),
}

# MHC studio: dark brown with a little grey. WhiteAmount 0.36 reads as blonde outdoors.
HAIR_COLOR = {
    "hairMelanin": 0.74,
    "Melanin": 0.74,
    "hairRedness": 0.10,
    "Redness": 0.10,
    "WhiteAmount": 0.16,
    "Whiteness": 0.16,
    "HairRoughness": 0.32,
    "Roughness": 0.32,
    "LightAmount": 0.0,
    "Lightness": 0.0,
}
BEARD_COLOR = {
    **HAIR_COLOR,
    "hairMelanin": 0.84,
    "Melanin": 0.84,
    "WhiteAmount": 0.10,
    "Whiteness": 0.10,
}
BROW_COLOR = {
    **HAIR_COLOR,
    "hairMelanin": 0.88,
    "Melanin": 0.88,
    "WhiteAmount": 0.06,
    "Whiteness": 0.06,
}

_color_mats: dict[str, list] = {}

REPORT = "FixRealityErrolNativeGrooms.txt"
_lines: list[str] = []


def log(msg: str) -> None:
    _lines.append(msg)
    unreal.log(f"[RealityErrolNativeGrooms] {msg}")


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


def unique_by_label(bp, label: str) -> list:
    seen: set[int] = set()
    found = []
    for comp_label, component, _h, _d in wiring.iter_all_components(bp):
        if comp_label.lower() != label.lower():
            continue
        if id(component) in seen:
            continue
        seen.add(id(component))
        found.append(component)
    return found


def dump_character_wardrobe() -> None:
    char = unreal.load_asset(CHAR_PATH)
    if not char:
        log(f"WARN: could not load {CHAR_PATH}")
        return
    log(f"Character {char.get_class().get_name()} {CHAR_PATH}")
    wardrobe = _prop(char, ("wardrobe_individual_assets", "WardrobeIndividualAssets"))
    if wardrobe:
        try:
            for slot, bundle in dict(wardrobe).items():
                items = _prop(bundle, ("items", "Items")) or []
                names = []
                for item in items:
                    try:
                        names.append(str(item))
                    except Exception:
                        names.append(_asset_path(item))
                log(f"  wardrobe[{slot}]: {names}")
        except Exception as exc:
            log(f"  WARN wardrobe dump: {exc}")
    pipelines = _prop(char, ("pipelines_per_class", "PipelinesPerClass"))
    if pipelines:
        try:
            for cls, pipe in dict(pipelines).items():
                log(f"  pipeline {cls}: {pipe}")
                if pipe:
                    for attr in (
                        "slot_names",
                        "SlotNames",
                        "selections",
                        "Selections",
                    ):
                        try:
                            log(f"    {attr}={pipe.get_editor_property(attr)}")
                        except Exception:
                            pass
        except Exception as exc:
            log(f"  WARN pipeline dump: {exc}")


def remove_costume(bp) -> int:
    removed = 0
    seen: set[int] = set()
    for label, component, _h, _d in wiring.iter_all_components(bp):
        if "SkeletalMesh" not in component.get_class().get_name():
            continue
        if label in {"Body", "Face"}:
            continue
        if any(
            tok.lower() in label.lower()
            for tok in ("Hair", "Beard", "Eyebrow", "Eyelash", "Mustache", "Fuzz", "Groom")
        ):
            continue
        if id(component) in seen:
            continue
        seen.add(id(component))
        mesh = _prop(component, ("skeletal_mesh", "SkeletalMesh"))
        path = _asset_path(mesh)
        log(f"Clearing clothing {label}: {path}")
        wiring.set_prop(component, ["skeletal_mesh", "SkeletalMesh", "SkinnedAsset"], None)
        try:
            component.set_skeletal_mesh_asset(None)
        except Exception:
            pass
        wiring.set_prop(
            component,
            ["relative_scale3d", "RelativeScale3D"],
            unreal.Vector(1.0, 1.0, 1.0),
        )
        wiring.set_prop(component, ["b_hidden_in_game", "bHiddenInGame"], True)
        wiring.set_prop(component, ["visible", "bVisible"], False)
        wiring.set_prop(component, ["leader_pose_component", "LeaderPoseComponent"], None)
        removed += 1
    return removed


def dump_skm_comp(prefix: str, comp) -> None:
    if not comp:
        log(f"{prefix}: (none)")
        return
    mesh = None
    try:
        mesh = comp.get_skeletal_mesh_asset()
    except Exception:
        mesh = _prop(comp, ("skeletal_mesh", "SkeletalMesh"))
    scale = _prop(comp, ("relative_scale3d", "RelativeScale3D"))
    hidden = _prop(comp, ("b_hidden_in_game", "bHiddenInGame"))
    visible = _prop(comp, ("visible", "bVisible"))
    anim = _prop(comp, ("anim_class", "AnimClass"))
    mats = []
    try:
        for mat in list(comp.get_materials() or []):
            mats.append(_asset_path(mat))
    except Exception:
        pass
    log(
        f"{prefix}: mesh={_asset_path(mesh)} scale={scale} hidden={hidden} "
        f"vis={visible} anim={_asset_path(anim)} mats={mats}"
    )


def dump_mesh_asset(path: str) -> None:
    mesh = unreal.load_asset(path)
    if not mesh:
        log(f"MESH {path}: MISSING")
        return
    skel = _prop(mesh, ("skeleton", "Skeleton"))
    phys = _prop(mesh, ("physics_asset", "PhysicsAsset"))
    log(f"MESH {path} skeleton={_asset_path(skel)} phys={_asset_path(phys)}")
    try:
        box = mesh.get_bounds()
        log(f"  bounds origin={box.origin} extent={box.box_extent}")
    except Exception as exc:
        log(f"  bounds: {exc}")


def dump_body_comparison(bp) -> None:
    donor = unreal.load_asset(DONOR_BP)
    dump_mesh_asset(BODY_MESH)
    dump_mesh_asset(FACE_MESH)
    for name, asset in (("DONOR", donor), ("PERFORMER", bp)):
        if not asset:
            log(f"{name} BP missing")
            continue
        body, _ = wiring.find_component(asset, "Body")
        face, _ = wiring.find_component(asset, "Face")
        dump_skm_comp(f"{name} Body", body)
        dump_skm_comp(f"{name} Face", face)
        for label, component, _h, _d in wiring.iter_all_components(asset):
            cls = component.get_class().get_name()
            if "SkeletalMesh" not in cls:
                continue
            path = _asset_path(_prop(component, ("skeletal_mesh", "SkeletalMesh")))
            if path != "(none)":
                log(f"{name} SKM {label}: {path}")


def apply_mesh_to_comp(comp, mesh) -> None:
    wiring.set_prop(comp, ["skeletal_mesh", "SkeletalMesh", "SkinnedAsset"], mesh)
    try:
        comp.set_skeletal_mesh_asset(mesh)
    except Exception:
        pass
    wiring.set_prop(
        comp, ["relative_scale3d", "RelativeScale3D"], unreal.Vector(1.0, 1.0, 1.0)
    )
    wiring.set_prop(comp, ["b_hidden_in_game", "bHiddenInGame"], False)
    wiring.set_prop(comp, ["visible", "bVisible"], True)


def copy_override_materials(src, dst) -> None:
    if not src or not dst:
        return
    try:
        mats = list(src.get_materials() or [])
    except Exception:
        mats = []
    clear_override_materials(dst)
    for index, mat in enumerate(mats):
        if not mat:
            continue
        try:
            dst.set_material(index, mat)
        except Exception:
            pass


def sync_body_and_face(bp) -> None:
    """Force Body/Face onto assembled RealityErrol meshes; keep exhibit anim classes."""
    body_mesh = unreal.load_asset(BODY_MESH)
    face_mesh = unreal.load_asset(FACE_MESH)
    if not body_mesh or not face_mesh:
        raise RuntimeError(f"Missing {BODY_MESH} or {FACE_MESH}")
    donor = unreal.load_asset(DONOR_BP)
    body, _ = wiring.find_component(bp, "Body")
    face, _ = wiring.find_component(bp, "Face")
    if not body or not face:
        raise RuntimeError("Performer missing Body/Face")
    donor_body, _ = wiring.find_component(donor, "Body") if donor else (None, None)
    donor_face, _ = wiring.find_component(donor, "Face") if donor else (None, None)
    apply_mesh_to_comp(body, body_mesh)
    apply_mesh_to_comp(face, face_mesh)
    copy_override_materials(donor_body, body)
    copy_override_materials(donor_face, face)
    log(
        f"Body AnimClass kept={_asset_path(_prop(body, ('anim_class', 'AnimClass')))} "
        f"(donor={_asset_path(_prop(donor_body, ('anim_class', 'AnimClass'))) if donor_body else '(none)'})"
    )
    dump_skm_comp("PERFORMER Body after sync", body)
    dump_skm_comp("PERFORMER Face after sync", face)


def sync_actor_body(actor) -> None:
    body_mesh = unreal.load_asset(BODY_MESH)
    face_mesh = unreal.load_asset(FACE_MESH)
    try:
        scale = actor.get_actor_scale3d()
        log(f"World actor scale={scale}")
        if (
            abs(float(scale.x) - 1.0) > 0.001
            or abs(float(scale.y) - 1.0) > 0.001
            or abs(float(scale.z) - 1.0) > 0.001
        ):
            actor.set_actor_scale3d(unreal.Vector(1.0, 1.0, 1.0))
            log("Reset world actor scale to 1,1,1")
    except Exception as exc:
        log(f"WARN actor scale: {exc}")
    try:
        comps = list(actor.get_components_by_class(unreal.SkeletalMeshComponent) or [])
    except Exception:
        comps = []
    for skm in comps:
        name = str(skm.get_name())
        dump_skm_comp(f"World before {name}", skm)
        if name == "Body":
            apply_mesh_to_comp(skm, body_mesh)
        elif name == "Face":
            apply_mesh_to_comp(skm, face_mesh)
        dump_skm_comp(f"World after {name}", skm)


def clear_hide_mask(bp) -> None:
    body, _ = wiring.find_component(bp, "Body")
    if not body:
        return
    try:
        materials = list(body.get_materials() or [])
    except Exception:
        materials = []
    for mat in materials:
        if not mat:
            continue
        try:
            unreal.MaterialEditingLibrary.set_material_instance_texture_parameter_value(
                mat, "HideMask", None
            )
            log(f"Cleared HideMask on {mat.get_name()}")
        except Exception as exc:
            log(f"WARN clear HideMask: {exc}")


def make_binding(groom, target_face, dest_path: str):
    if unreal.EditorAssetLibrary.does_asset_exist(dest_path):
        existing = unreal.load_asset(dest_path)
        if existing:
            return existing
    if not unreal.EditorAssetLibrary.does_directory_exist(BINDING_DIR):
        unreal.EditorAssetLibrary.make_directory(BINDING_DIR)
    library = getattr(unreal, "GroomLibrary", None) or getattr(
        unreal, "GroomBlueprintLibrary", None
    )
    if not library:
        return None
    try:
        return library.create_new_groom_binding_asset_with_path(
            dest_path, groom, target_face, 100, None, 0
        )
    except TypeError:
        try:
            return library.create_new_groom_binding_asset_with_path(
                in_desired_package_path=dest_path,
                in_groom_asset=groom,
                in_skeletal_mesh=target_face,
                in_num_interpolation_points=100,
            )
        except Exception as exc:
            log(f"WARN binding: {exc}")
            return None
    except Exception as exc:
        log(f"WARN binding: {exc}")
        return None


def clear_override_materials(comp) -> None:
    try:
        count = int(comp.get_num_materials())
    except Exception:
        count = 8
    for index in range(max(count, 8)):
        try:
            comp.set_material(index, None)
        except Exception:
            pass
    wiring.set_prop(comp, ["override_materials", "OverrideMaterials"], [])


def groom_group_materials(groom) -> list:
    groups = None
    for getter in (
        lambda: groom.get_hair_groups_materials(),
        lambda: groom.get_editor_property("hair_groups_materials"),
        lambda: groom.get_editor_property("HairGroupsMaterials"),
    ):
        try:
            groups = getter()
            if groups:
                break
        except Exception:
            continue
    materials = []
    for group in groups or []:
        mat = None
        for name in ("material", "Material"):
            try:
                mat = group.get_editor_property(name)
            except Exception:
                mat = getattr(group, name, None)
            if mat:
                break
        materials.append(mat)
    return materials


def apply_groom_own_materials(comp, groom) -> None:
    clear_override_materials(comp)
    for index, mat in enumerate(groom_group_materials(groom)):
        if not mat:
            continue
        try:
            comp.set_material(index, mat)
            log(f"    material[{index}]={_asset_path(mat)}")
        except Exception as exc:
            log(f"    WARN material[{index}]: {exc}")


def _set_groom_properties(comp, groom, binding) -> None:
    """Write GroomAsset/BindingAsset into instance records.

    Do not call SetGroomAsset/PostEditChange here: those restore the serialized
    Hair_S_Clean instance override on World Partition actors.
    """
    try:
        comp.modify()
    except Exception:
        pass
    used_groom = wiring.set_prop(comp, ["groom_asset", "GroomAsset"], groom)
    log(f"    set_prop GroomAsset via {used_groom}")
    if binding:
        used_bind = wiring.set_prop(
            comp,
            ["binding_asset", "BindingAsset", "GroomBindingAsset"],
            binding,
        )
        log(f"    set_prop BindingAsset via {used_bind}")


def ensure_mic(name: str, parent_path: str, params: dict[str, float]):
    dest = f"{MI_DIR}/{name}"
    if not unreal.EditorAssetLibrary.does_directory_exist(MI_DIR):
        unreal.EditorAssetLibrary.make_directory(MI_DIR)
    parent = unreal.load_asset(parent_path)
    if not parent:
        raise RuntimeError(f"Missing parent {parent_path}")
    if unreal.EditorAssetLibrary.does_asset_exist(dest):
        mic = unreal.load_asset(dest)
    else:
        factory = unreal.MaterialInstanceConstantFactoryNew()
        tools = unreal.AssetToolsHelpers.get_asset_tools()
        mic = tools.create_asset(name, MI_DIR, unreal.MaterialInstanceConstant, factory)
        if not mic:
            raise RuntimeError(f"Could not create {dest}")
    try:
        mic.set_editor_property("parent", parent)
    except Exception:
        try:
            unreal.MaterialEditingLibrary.set_material_instance_parent(mic, parent)
        except Exception:
            pass
    lib = unreal.MaterialEditingLibrary
    for param, value in params.items():
        try:
            lib.set_material_instance_scalar_parameter_value(mic, param, float(value))
        except Exception:
            continue
    try:
        mic.modify()
    except Exception:
        pass
    unreal.EditorAssetLibrary.save_loaded_asset(mic, only_if_is_dirty=False)
    log(f"MIC {dest} {params}")
    return mic


def build_color_materials() -> None:
    """Replace plugin-blonde facial hair with MHC dark brown + grey."""
    global _color_mats
    hair_mi = ensure_mic("MI_RealityErrol_Hair", PLUGIN_HAIR_MI, HAIR_COLOR)
    hair_cards = ensure_mic("MI_RealityErrol_Hair_Cards", PLUGIN_CARDS_MI, HAIR_COLOR)
    hair_helmet = ensure_mic("MI_RealityErrol_Hair_Helmet", PLUGIN_HELMET_MI, HAIR_COLOR)
    facial_mi = ensure_mic("MI_RealityErrol_FacialHair", PLUGIN_FACIAL_MI, BEARD_COLOR)
    brow_mi = ensure_mic("MI_RealityErrol_Eyebrows", PLUGIN_FACIAL_MI, BROW_COLOR)
    _color_mats = {
        "Hair": [hair_mi, hair_cards, hair_helmet],
        "Beard": [facial_mi, hair_mi, hair_helmet],
        "Mustache": [facial_mi, hair_mi, hair_helmet],
        "Eyebrows": [brow_mi, facial_mi],
        "Eyelashes": [hair_mi],
    }


def apply_colored_materials(comp, label: str | None) -> None:
    mats = _color_mats.get(label or "") if label else None
    if not mats:
        return
    clear_override_materials(comp)
    for index, mat in enumerate(mats):
        if not mat:
            continue
        try:
            comp.set_material(index, mat)
            log(f"    color[{index}]={mat.get_name()}")
        except Exception as exc:
            log(f"    WARN color[{index}]: {exc}")


def assign_groom(
    comp, groom, binding, face, label: str | None = None, *, live_set: bool = True
) -> None:
    clear_override_materials(comp)
    _set_groom_properties(comp, groom, binding)
    # World Partition instance: SetGroomAsset can restore the serialized Hair_S_Clean
    # override. Property writes + CDO revert are more likely to persist headless.
    if live_set:
        try:
            comp.set_groom_asset(groom)
        except Exception:
            pass
        if binding:
            try:
                comp.set_binding_asset(binding)
            except Exception:
                pass
    apply_colored_materials(comp, label)
    wiring.set_prop(comp, ["b_hidden_in_game", "bHiddenInGame"], False)
    wiring.set_prop(comp, ["visible", "bVisible"], True)
    if face:
        try:
            comp.attach_to_component(
                face,
                unreal.Name(""),
                unreal.AttachmentRule.KEEP_RELATIVE,
                unreal.AttachmentRule.KEEP_RELATIVE,
                unreal.AttachmentRule.KEEP_RELATIVE,
                False,
            )
        except Exception:
            pass
        try:
            comp.add_collision_component(face)
        except Exception:
            pass


def groom_bundle(label: str):
    groom_path, plugin_bind = GROOM_MAP[label]
    if not unreal.EditorAssetLibrary.does_asset_exist(groom_path):
        raise RuntimeError(f"Missing RealityErrol groom {groom_path}")
    groom = unreal.load_asset(groom_path)
    dest_bind = f"{BINDING_DIR}/{os.path.basename(groom_path)}_RealityErrol_NativeBinding"
    binding = make_binding(groom, unreal.load_asset(FACE_MESH), dest_bind)
    if not binding and unreal.EditorAssetLibrary.does_asset_exist(plugin_bind):
        binding = unreal.load_asset(plugin_bind)
        log(f"  {label}: plugin binding fallback {plugin_bind}")
    return groom, binding, groom_path


def apply_native_grooms(bp) -> int:
    face, _ = wiring.find_component(bp, "Face")
    if not unreal.load_asset(FACE_MESH):
        raise RuntimeError(f"Missing {FACE_MESH}")
    assigned = 0
    for label in GROOM_MAP:
        groom, binding, groom_path = groom_bundle(label)
        comps = unique_by_label(bp, label)
        if not comps:
            log(f"WARN: no {label} component")
            continue
        for comp in comps:
            assign_groom(comp, groom, binding, face, label)
            log(
                f"{label} <- {groom_path} bind={_asset_path(_prop(comp, ('binding_asset', 'BindingAsset')))}"
            )
            assigned += 1
    return assigned


def _banned_hits(path: str) -> list[str]:
    return [token for token in BANNED_GROOMS if token in path]


def dump_groom_state(prefix: str, label: str, comp) -> None:
    asset = _asset_path(_prop(comp, ("groom_asset", "GroomAsset")))
    bind = _asset_path(_prop(comp, ("binding_asset", "BindingAsset")))
    mats = []
    try:
        count = int(comp.get_num_materials())
    except Exception:
        count = 0
    for index in range(count):
        try:
            mats.append(_asset_path(comp.get_material(index)))
        except Exception:
            pass
    log(f"{prefix}{label}: asset={asset} bind={bind} mats={mats}")


def assert_groom_comp(map_key: str, comp) -> None:
    path = _asset_path(_prop(comp, ("groom_asset", "GroomAsset")))
    hits = _banned_hits(path)
    if hits:
        raise RuntimeError(f"{map_key} still on old Errol groom {path}")
    if path == "(none)":
        raise RuntimeError(f"{map_key} GroomAsset empty")
    expected = os.path.basename(GROOM_MAP[map_key][0])
    if expected not in path:
        raise RuntimeError(f"{map_key} expected {expected}, got {path}")
    try:
        count = int(comp.get_num_materials())
    except Exception:
        count = 0
    for index in range(count):
        try:
            mat_path = _asset_path(comp.get_material(index))
        except Exception:
            continue
        mat_hits = _banned_hits(mat_path)
        if mat_hits:
            raise RuntimeError(f"{map_key} still using old Errol material {mat_path}")


def assert_not_old_errol_grooms(bp) -> None:
    for label in GROOM_MAP:
        for comp in unique_by_label(bp, label):
            dump_groom_state("BP ", label, comp)
            assert_groom_comp(label, comp)


def _component_label(comp) -> str:
    for name in (comp.get_name(), getattr(comp, "get_fname", lambda: "")()):
        text = str(name)
        for label in GROOM_MAP:
            if text.lower() == label.lower() or text.lower().startswith(label.lower()):
                return label
    return ""


def strip_actor_costume(actor) -> int:
    removed = 0
    skm_cls = getattr(unreal, "SkeletalMeshComponent", None)
    if not skm_cls:
        return 0
    try:
        comps = list(actor.get_components_by_class(skm_cls) or [])
    except Exception:
        comps = []
    for comp in comps:
        name = str(comp.get_name())
        if name in {"Body", "Face"}:
            continue
        if any(
            tok.lower() in name.lower()
            for tok in ("Hair", "Beard", "Eyebrow", "Eyelash", "Mustache", "Fuzz", "Groom")
        ):
            continue
        mesh = _prop(comp, ("skeletal_mesh", "SkeletalMesh"))
        path = _asset_path(mesh)
        if path == "(none)":
            continue
        log(f"World clothing {actor.get_actor_label()}.{name}: {path}")
        wiring.set_prop(comp, ["skeletal_mesh", "SkeletalMesh", "SkinnedAsset"], None)
        try:
            comp.set_skeletal_mesh_asset(None)
        except Exception:
            pass
        wiring.set_prop(comp, ["relative_scale3d", "RelativeScale3D"], unreal.Vector(1.0, 1.0, 1.0))
        wiring.set_prop(comp, ["b_hidden_in_game", "bHiddenInGame"], True)
        wiring.set_prop(comp, ["visible", "bVisible"], False)
        clear_override_materials(comp)
        removed += 1
    return removed


def try_revert_instance_overrides(actor) -> None:
    """Drop Hair_S_Clean / body-scale instance overrides so the CDO can win."""
    subsystem = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    lib = unreal.SubobjectDataBlueprintFunctionLibrary
    if not subsystem or not hasattr(subsystem, "k2_gather_subobject_data_for_instance"):
        return
    methods = [name for name in dir(subsystem) if "default" in name.lower() or "revert" in name.lower()]
    log(f"Subobject revert methods: {methods}")
    try:
        handles = subsystem.k2_gather_subobject_data_for_instance(actor) or []
    except Exception as exc:
        log(f"WARN instance subobjects: {exc}")
        return
    for handle in handles:
        data = lib.get_data(handle)
        if not data or not lib.is_component(data):
            continue
        obj = None
        for getter in (
            lambda: lib.get_associated_object(data),
            lambda: lib.get_object(data),
        ):
            try:
                obj = getter()
            except Exception:
                obj = None
            if obj:
                break
        if not obj:
            continue
        name = str(obj.get_name())
        cls = obj.get_class().get_name()
        if "Groom" not in cls and name not in {"Body", "Face"}:
            continue
        for owner, fn_name in (
            (subsystem, "revert_to_default"),
            (subsystem, "reset_to_default"),
            (subsystem, "k2_revert_to_default"),
            (subsystem, "restore_subobject_to_default"),
            (lib, "revert_to_default"),
            (lib, "reset_to_default"),
        ):
            fn = getattr(owner, fn_name, None)
            if not callable(fn):
                continue
            try:
                fn(handle)
                log(f"Reverted {name} via {fn_name}")
                break
            except TypeError:
                try:
                    fn(handle, True)
                    log(f"Reverted {name} via {fn_name}(handle, True)")
                    break
                except Exception as exc:
                    log(f"  {fn_name}: {exc}")
            except Exception as exc:
                log(f"  {fn_name}: {exc}")


def iter_instance_groom_components(actor):
    """Prefer instance subobject records so ICH overrides actually change."""
    yielded = False
    subsystem = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    lib = unreal.SubobjectDataBlueprintFunctionLibrary
    if subsystem and hasattr(subsystem, "k2_gather_subobject_data_for_instance"):
        try:
            handles = subsystem.k2_gather_subobject_data_for_instance(actor) or []
        except Exception as exc:
            log(f"WARN instance subobjects: {exc}")
            handles = []
        for handle in handles:
            data = lib.get_data(handle)
            if not data or not lib.is_component(data):
                continue
            obj = None
            for getter in (
                lambda: lib.get_associated_object(data),
                lambda: lib.get_object(data),
            ):
                try:
                    obj = getter()
                except Exception:
                    obj = None
                if obj:
                    break
            if not obj or "Groom" not in obj.get_class().get_name():
                continue
            label = _component_label(obj)
            if not label:
                try:
                    label = str(lib.get_variable_name(data))
                except Exception:
                    label = ""
            yielded = True
            yield label, obj
    if yielded:
        return
    groom_cls = getattr(unreal, "GroomComponent", None)
    if not groom_cls:
        return
    try:
        comps = list(actor.get_components_by_class(groom_cls) or [])
    except Exception:
        comps = []
    for comp in comps:
        yield _component_label(comp), comp


def apply_native_grooms_to_actor(actor) -> int:
    assigned = 0
    try_revert_instance_overrides(actor)
    sync_actor_body(actor)
    face = None
    try:
        for skm in actor.get_components_by_class(unreal.SkeletalMeshComponent) or []:
            if str(skm.get_name()) == "Face":
                face = skm
                break
    except Exception:
        face = None
    by_label: dict[str, list] = {label: [] for label in GROOM_MAP}
    for label, comp in iter_instance_groom_components(actor):
        if label not in by_label:
            continue
        by_label[label].append(comp)
        dump_groom_state("World before ", label, comp)
    for label in GROOM_MAP:
        if not by_label[label]:
            log(f"WARN: world actor has no {label} GroomComponent")
            continue
        groom, binding, groom_path = groom_bundle(label)
        for comp in by_label[label]:
            assign_groom(comp, groom, binding, face, label, live_set=False)
            path = _asset_path(_prop(comp, ("groom_asset", "GroomAsset")))
            if _banned_hits(path) or os.path.basename(GROOM_MAP[label][0]) not in path:
                log(f"World {label} still {path} after property write — trying SetGroomAsset")
                assign_groom(comp, groom, binding, face, label, live_set=True)
            dump_groom_state("World after ", label, comp)
            assert_groom_comp(label, comp)
            assigned += 1
            log(f"World {label} <- {groom_path}")
    return assigned


def ensure_godfrey_world_loaded() -> None:
    world = unreal.EditorLevelLibrary.get_editor_world()
    path = ""
    try:
        path = world.get_path_name() if world else ""
    except Exception:
        path = ""
    if "Godfrey_World" not in str(path):
        log(f"Loading {LEVEL_PATH} (current world={path or 'none'})")
        unreal.EditorLoadingAndSavingUtils.load_map(LEVEL_PATH)
        world = unreal.EditorLevelLibrary.get_editor_world()
    if world:
        unreal.SystemLibrary.execute_console_command(world, "wp.Editor.LoadAllCells")
        log("wp.Editor.LoadAllCells")


def save_world_actor(actor) -> None:
    try:
        actor.modify()
    except Exception:
        pass
    pkg = None
    for getter in (
        lambda: actor.get_package(),
        lambda: actor.get_outermost(),
        lambda: actor.get_outer(),
    ):
        try:
            pkg = getter()
        except Exception:
            pkg = None
        if pkg:
            break
    if pkg:
        try:
            pkg.mark_package_dirty()
        except Exception:
            pass
        path = pkg.get_name()
        log(f"Actor package {path}")
        try:
            ok = unreal.EditorLoadingAndSavingUtils.save_packages([pkg], False)
            log(f"save_packages(pkg)={ok}")
        except Exception as exc:
            log(f"save_packages(pkg): {exc}")
        try:
            ok = unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False)
            log(f"save_asset({path})={ok}")
        except Exception as exc:
            log(f"save_asset: {exc}")
    try:
        les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
        les.save_current_level()
        log("LevelEditorSubsystem.save_current_level")
    except Exception as exc:
        log(f"save_current_level: {exc}")
    try:
        world = unreal.UnrealEditorSubsystem().get_editor_world()
    except Exception:
        world = unreal.EditorLevelLibrary.get_editor_world()
    try:
        unreal.EditorLoadingAndSavingUtils.save_map(world, LEVEL_PATH)
        log(f"save_map {LEVEL_PATH}")
    except Exception as exc:
        log(f"save_map: {exc}")
    try:
        unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
        log("Saved dirty world packages")
    except Exception as exc:
        log(f"WARN save_dirty_packages: {exc}")


def tag_groom_component(comp) -> None:
    try:
        tags = list(comp.component_tags or [])
    except Exception:
        tags = []
    marker = unreal.Name("RealityErrolNativeGroom")
    if marker not in tags and "RealityErrolNativeGroom" not in [str(t) for t in tags]:
        tags.append(marker)
        try:
            comp.component_tags = tags
        except Exception:
            wiring.set_prop(comp, ["component_tags", "ComponentTags"], tags)


def patch_world_performer() -> int:
    ensure_godfrey_world_loaded()
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    if not eas:
        log("WARN: EditorActorSubsystem unavailable — world actor not patched")
        return 0
    actors = []
    for actor in eas.get_all_level_actors() or []:
        if not actor:
            continue
        try:
            label = actor.get_actor_label()
            name = actor.get_name()
        except Exception:
            continue
        if (
            label == "BP_Godfrey_Performer"
            or "BP_Godfrey_Performer" in name
            or wiring.GODFREY_CHARACTER_TAG in list(actor.tags)
        ):
            actors.append(actor)
    if not actors:
        log("WARN: no BP_Godfrey_Performer actor loaded in Godfrey_World")
        return 0
    assigned = 0
    txn = None
    try:
        txn = unreal.ScopedEditorTransaction("RealityErrol native grooms on world performer")
        txn.__enter__()
    except Exception as exc:
        log(f"WARN transaction: {exc}")
        txn = None
    try:
        for actor in actors:
            log(f"Patching world actor {actor.get_actor_label()} ({actor.get_name()})")
            try:
                actor.modify()
            except Exception:
                pass
            n_costume = strip_actor_costume(actor)
            log(f"World clothing slots cleared: {n_costume}")
            assigned += apply_native_grooms_to_actor(actor)
            for _label, comp in iter_instance_groom_components(actor):
                tag_groom_component(comp)
        if assigned:
            for actor in actors:
                save_world_actor(actor)
    finally:
        if txn:
            try:
                txn.__exit__(None, None, None)
            except Exception:
                pass
    return assigned


def apply_live_to_loaded_actors() -> int:
    """Set grooms+colors on the placed performer without saving the level.

    Used by Content/Python/init_unreal.py so the editor viewport/PIE ignore
    Hair_S_Clean instance overrides even when World Partition will not serialize.
    """
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    if not eas:
        return 0
    assigned = 0
    for actor in eas.get_all_level_actors() or []:
        if not actor:
            continue
        try:
            label = actor.get_actor_label()
            name = actor.get_name()
        except Exception:
            continue
        if label != "BP_Godfrey_Performer" and "BP_Godfrey_Performer" not in name:
            continue
        assigned += apply_native_grooms_to_actor(actor)
    return assigned


def main() -> None:
    log("=== RealityErrol native grooms + body sync + remove costume + world instance ===")
    dump_character_wardrobe()
    bp = unreal.load_asset(wiring.GODFREY_PERFORMER_BP)
    if not bp:
        raise RuntimeError(f"Missing {wiring.GODFREY_PERFORMER_BP}")

    dump_body_comparison(bp)
    sync_body_and_face(bp)
    n_costume = remove_costume(bp)
    log(f"Removed clothing slots: {n_costume}")
    clear_hide_mask(bp)

    build_color_materials()
    n_grooms = apply_native_grooms(bp)
    if n_grooms < 5:
        raise RuntimeError(f"Expected 5 groom types, assigned {n_grooms}")
    assert_not_old_errol_grooms(bp)

    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    if not wiring.save_godfrey_performer_blueprint(bp):
        raise RuntimeError(wiring.BP_SAVE_LOCK_HINT)

    n_world = patch_world_performer()
    if n_world < 5:
        log(
            f"WARN: world actor patched {n_world} grooms (CDO is correct). "
            "If PIE still shows Hair_S_Clean, run this script from the open editor."
        )

    write_report(True)
    log(
        "PASS — Blueprint uses Hair_S_Casual / Beard_S_Uneven / Mustache_L_Full "
        "with dark-brown salt-and-pepper materials. Body/Face forced to "
        "SKM_MH_RealityErrol_*. Costume cleared. World instance patched if loaded."
    )


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        unreal.log_error(f"[RealityErrolNativeGrooms] {exc}")
        write_report(False)
        sys.exit(1)
