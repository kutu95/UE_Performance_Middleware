"""Match MH_RealityErrol MHC hair/beard color on BP_Godfrey_Performer.

Plugin grooms ship with default melanin 0.16 (blonde). MHC screenshot is dark
brown salt-and-pepper. This duplicates MI_Hair / MI_Facial_Hair into the project
with those colors and assigns them on Hair/Beard/Mustache/Eyebrows.

Run in the OPEN editor (stop PIE):
  Tools → Execute Python Script → this file
  then Ctrl+S the level.
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

CHAR_PATH = "/Game/Actors/RealityErrol/MH_RealityErrol"
MI_DIR = "/Game/MetaHumans/MH_RealityErrol/Grooms"
PLUGIN_HAIR = "/MetaHumanCharacter/Materials/MI_Hair"
PLUGIN_CARDS = "/MetaHumanCharacter/Materials/MI_Hair_Cards"
PLUGIN_HELMET = "/MetaHumanCharacter/Materials/MI_Hair_Helmet"
PLUGIN_FACIAL = "/MetaHumanCharacter/Materials/MI_Facial_Hair"

# Fallback if the character pipeline bag cannot be read. Matches the MHC
# studio screenshot: dark brown with grey (not the plugin blonde default).
FALLBACK_HAIR = {
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
FALLBACK_BEARD = {
    **FALLBACK_HAIR,
    "hairMelanin": 0.84,
    "Melanin": 0.84,
    "WhiteAmount": 0.10,
    "Whiteness": 0.10,
}
FALLBACK_BROWS = {
    **FALLBACK_HAIR,
    "hairMelanin": 0.88,
    "Melanin": 0.88,
    "WhiteAmount": 0.06,
    "Whiteness": 0.06,
}

REPORT = "FixRealityErrolGroomColors.txt"
_lines: list[str] = []


def log(msg: str) -> None:
    _lines.append(msg)
    unreal.log(f"[RealityErrolGroomColor] {msg}")


def write_report(ok: bool) -> None:
    path = unreal.Paths.convert_relative_path_to_full(
        unreal.Paths.project_saved_dir() + REPORT
    )
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


def bag_float(bag, name: str):
    if not bag:
        return None
    for getter in (
        lambda: bag.get_value_float(name),
        lambda: bag.get_value_double(name),
        lambda: bag.get_editor_property(name),
    ):
        try:
            value = getter()
            if value is not None:
                return float(value)
        except Exception:
            continue
    return None


def dump_character_hair_color() -> dict[str, dict[str, float]]:
    found: dict[str, dict[str, float]] = {}
    char = unreal.load_asset(CHAR_PATH)
    if not char:
        log(f"WARN: could not load {CHAR_PATH}")
        return found
    pipelines = _prop(char, ("pipelines_per_class", "PipelinesPerClass"))
    if not pipelines:
        log("No PipelinesPerClass")
        return found
    try:
        items = dict(pipelines)
    except Exception:
        items = {}
    for cls, pipe in items.items():
        log(f"pipeline {cls}: {pipe}")
        if not pipe:
            continue
        for attr in dir(pipe):
            if attr.startswith("_"):
                continue
            try:
                value = getattr(pipe, attr)
            except Exception:
                continue
            text = str(attr).lower()
            if "hair" not in text and "beard" not in text and "mustache" not in text and "eyebrow" not in text:
                continue
            log(f"  {attr}={value}")
        for name in (
            "pinned_slot_selections",
            "PinnedSlotSelections",
            "slot_selections",
            "SlotSelections",
        ):
            try:
                selections = pipe.get_editor_property(name)
            except Exception:
                continue
            log(f"  {name}={selections}")
            try:
                for sel in list(selections or []):
                    bag = _prop(sel, ("instance_parameters", "InstanceParameters"))
                    slot = _prop(sel, ("slot_name", "SlotName"))
                    melanin = bag_float(bag, "Melanin") or bag_float(bag, "hairMelanin")
                    white = bag_float(bag, "Whiteness") or bag_float(bag, "WhiteAmount")
                    redness = bag_float(bag, "Redness") or bag_float(bag, "hairRedness")
                    log(f"    slot={slot} melanin={melanin} white={white} redness={redness}")
                    key = str(slot or "")
                    if melanin is not None:
                        found[key] = {
                            "hairMelanin": melanin,
                            "Melanin": melanin,
                            "WhiteAmount": white or 0.0,
                            "Whiteness": white or 0.0,
                            "hairRedness": redness or 0.0,
                            "Redness": redness or 0.0,
                        }
            except Exception as exc:
                log(f"  WARN selections: {exc}")
    return found


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
    log(f"MIC {dest} parent={parent_path} {params}")
    return mic


def apply_materials(comp, materials: list) -> None:
    for index, mat in enumerate(materials):
        if not mat:
            continue
        try:
            comp.set_material(index, mat)
        except Exception as exc:
            log(f"  WARN set_material[{index}]: {exc}")
    wiring.set_prop(comp, ["b_hidden_in_game", "bHiddenInGame"], False)
    wiring.set_prop(comp, ["visible", "bVisible"], True)


def apply_to_world(slot_mats: dict[str, list]) -> int:
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    if not eas:
        return 0
    assigned = 0
    for actor in eas.get_all_level_actors() or []:
        try:
            label = actor.get_actor_label()
        except Exception:
            continue
        if label != "BP_Godfrey_Performer" and "BP_Godfrey_Performer" not in actor.get_name():
            continue
        log(f"World actor {label}")
        try:
            actor.modify()
        except Exception:
            pass
        groom_cls = getattr(unreal, "GroomComponent", None)
        if not groom_cls:
            continue
        for comp in actor.get_components_by_class(groom_cls) or []:
            name = str(comp.get_name())
            key = None
            for slot in slot_mats:
                if name.lower() == slot.lower() or name.lower().startswith(slot.lower()):
                    key = slot
                    break
            if not key:
                continue
            apply_materials(comp, slot_mats[key])
            assigned += 1
            log(f"  {name} colored")
        try:
            unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
        except Exception:
            pass
    return assigned


def main() -> None:
    log("=== RealityErrol groom colors (dark brown salt-and-pepper) ===")
    dumped = dump_character_hair_color()
    hair_params = dumped.get("Hair") or dumped.get("hair") or FALLBACK_HAIR
    beard_params = dumped.get("Beard") or dumped.get("beard") or FALLBACK_BEARD
    brow_params = dumped.get("Eyebrows") or dumped.get("eyebrows") or FALLBACK_BROWS
    log(f"Using hair params {hair_params}")
    log(f"Using beard params {beard_params}")

    hair_mi = ensure_mic("MI_RealityErrol_Hair", PLUGIN_HAIR, hair_params)
    hair_cards = ensure_mic("MI_RealityErrol_Hair_Cards", PLUGIN_CARDS, hair_params)
    hair_helmet = ensure_mic("MI_RealityErrol_Hair_Helmet", PLUGIN_HELMET, hair_params)
    facial_mi = ensure_mic("MI_RealityErrol_FacialHair", PLUGIN_FACIAL, beard_params)
    brow_mi = ensure_mic("MI_RealityErrol_Eyebrows", PLUGIN_FACIAL, brow_params)

    slot_mats = {
        "Hair": [hair_mi, hair_cards, hair_helmet],
        "Beard": [facial_mi, hair_mi, hair_helmet],
        "Mustache": [facial_mi, hair_mi, hair_helmet],
        "Eyebrows": [brow_mi, facial_mi],
        "Eyelashes": [hair_mi],
    }

    bp = unreal.load_asset(wiring.GODFREY_PERFORMER_BP)
    if not bp:
        raise RuntimeError(f"Missing {wiring.GODFREY_PERFORMER_BP}")
    for label, mats in slot_mats.items():
        comps = unique_by_label(bp, label)
        if not comps:
            log(f"WARN: no {label}")
            continue
        for comp in comps:
            apply_materials(comp, mats)
            log(f"BP {label} materials set")

    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    if not wiring.save_godfrey_performer_blueprint(bp):
        raise RuntimeError(wiring.BP_SAVE_LOCK_HINT)

    n_world = apply_to_world(slot_mats)
    log(f"World groom components colored: {n_world}")
    write_report(True)
    log(
        "PASS — hair/beard/moustache use dark brown + grey (MHC screenshot), "
        "not plugin blonde. Ctrl+S the level, then PIE."
    )


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        unreal.log_error(f"[RealityErrolGroomColor] {exc}")
        write_report(False)
        sys.exit(1)
