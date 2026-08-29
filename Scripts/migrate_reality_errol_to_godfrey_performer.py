"""Replace BP_Godfrey_Performer with assembled MH_RealityErrol and re-wire
the exhibition ACE / lip-sync / performance stack.

Keeps soft path /Game/MetaHumans/Godfrey/BP_Godfrey_Performer so Godfrey_World labels,
queue poll fallbacks, and validators keep working.

Prerequisite: Assemble MH_RealityErrol so this exists:
  /Game/MetaHumans/MH_RealityErrol/BP_MH_RealityErrol
  /Game/MetaHumans/MH_RealityErrol/Body/SKM_MH_RealityErrol_BodyMesh
  /Game/MetaHumans/MH_RealityErrol/Face/SKM_MH_RealityErrol_FaceMesh

Does NOT spawn/destroy level actors (World Partition — do level swap in editor).

Headless:
  UnrealEditor-Cmd.exe "D:/UE Projects/MetaHuman_Baseline_UE58_Test/UnrealPerformer.uproject"
    -ExecutePythonScript="D:/UE Projects/MetaHuman_Baseline_UE58_Test/Scripts/migrate_reality_errol_to_godfrey_performer.py"
    -unattended -nop4 -nosplash -log

Editor (Godfrey_World open): Tools → Execute Python Script → this file.

After PASS: open Godfrey_World in editor and run
  Scripts/swap_godfrey_world_to_mhc_performer.py
  if the placed actor did not pick up the new shell.
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

DONOR_PACKAGE = "/Game/MetaHumans/MH_RealityErrol"
DONOR_BP = f"{DONOR_PACKAGE}/BP_MH_RealityErrol"
BODY_MESH = f"{DONOR_PACKAGE}/Body/SKM_MH_RealityErrol_BodyMesh"
FACE_MESH = f"{DONOR_PACKAGE}/Face/SKM_MH_RealityErrol_FaceMesh"
GODFREY_PACKAGE = "/Game/MetaHumans/Godfrey"
GODFREY_PERFORMER_BP = wiring.GODFREY_PERFORMER_BP
ARCHIVE_BP = "/Game/MetaHumans/Godfrey/BP_Godfrey_Performer_MHC_Errol_Archive"
GM_EXHIBIT = wiring.GM_GODFREY_EXHIBIT
SKIP_MESH_LABELS = {"Body", "Face"}
GROOM_TOKENS = ("Hair", "Beard", "Eyebrow", "Eyelash", "Mustache", "Fuzz", "Groom")
OUTFIT_FALLBACKS = (
    "/Game/MetaHumans/MHC_CaptainGodfrey/Clothing/MHC_CaptainGodfrey_Outfits1",
    "/Game/MetaHumans/MHC_CaptainGodfrey/Clothing/MHC_CaptainGodfrey_Outfits",
    "/Game/MetaHumans/MHC_Errol/Clothing/MHC_Errol_Outfits",
)
SKEL_MESH_COMPONENT_CLASS = "/Script/Engine.SkeletalMeshComponent"

REPORT = "MigrateRealityErrolToPerformer.txt"
_lines: list[str] = []


def log(msg: str) -> None:
    _lines.append(msg)
    unreal.log(f"[MigrateRealityErrol] {msg}")


def write_report(ok: bool) -> None:
    saved = unreal.Paths.project_saved_dir()
    path = unreal.Paths.convert_relative_path_to_full(saved + REPORT)
    if "UnrealEngine" in path.replace("\\", "/") and "MetaHuman_Baseline" not in path:
        path = r"D:\UE Projects\MetaHuman_Baseline_UE58_Test\Saved\\" + REPORT
    header = "RESULT: PASS\n" if ok else "RESULT: FAIL\n"
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(header + "\n".join(_lines) + "\n")
    log(f"Report: {path}")


def require_reality_errol_donor() -> None:
    missing = [
        path
        for path in (DONOR_BP, BODY_MESH, FACE_MESH)
        if not unreal.EditorAssetLibrary.does_asset_exist(path)
    ]
    if missing:
        raise RuntimeError(
            "Missing assembled RealityErrol donor assets:\n  "
            + "\n  ".join(missing)
            + "\nOpen MH_RealityErrol in MetaHuman Creator → Assembly → Assemble "
            "(output / name MH_RealityErrol) so Content/MetaHumans/MH_RealityErrol/ "
            "has BP_MH_RealityErrol and SKM_MH_RealityErrol_* meshes, then re-run."
        )
    log(f"Donor OK: {DONOR_BP} + RealityErrol body/face meshes")


def wait_for_asset_registry() -> None:
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    if hasattr(registry, "search_all_assets"):
        registry.search_all_assets(True)
    import time

    for _ in range(120):
        try:
            if not registry.is_loading_assets():
                break
        except Exception:
            break
        time.sleep(0.25)
    log("Asset registry ready")


def _component_labels(bp) -> list[str]:
    return [label for label, _comp, _h, _d in wiring.iter_all_components(bp)]


def _mesh_path(component) -> str:
    mesh = None
    try:
        mesh = component.get_skeletal_mesh_asset()
    except Exception:
        pass
    if not mesh:
        try:
            mesh = component.get_editor_property("skeletal_mesh")
        except Exception:
            mesh = None
    if not mesh:
        try:
            mesh = component.get_editor_property("SkeletalMesh")
        except Exception:
            mesh = None
    if not mesh:
        return "(none)"
    try:
        return mesh.get_path_name()
    except Exception:
        return str(mesh)


def _groom_asset_path(component) -> str:
    for name in ("groom_asset", "GroomAsset", "strands_groom_asset", "StrandsGroomAsset"):
        try:
            asset = component.get_editor_property(name)
        except Exception:
            asset = None
        if asset:
            try:
                return asset.get_path_name()
            except Exception:
                return str(asset)
    return "(none)"


def _is_groom_component(label: str, component) -> bool:
    cls = component.get_class().get_name()
    if "Groom" in cls:
        return True
    return any(token.lower() in label.lower() for token in GROOM_TOKENS)


def _is_hair_label(label: str) -> bool:
    lower = label.lower()
    if "eyebrow" in lower or "eyelash" in lower or "beard" in lower or "mustache" in lower:
        return False
    return "hair" in lower


def collect_grooms(bp) -> list[tuple[str, str, str]]:
    grooms: list[tuple[str, str, str]] = []
    for label, component, _h, _d in wiring.iter_all_components(bp):
        if not _is_groom_component(label, component):
            continue
        grooms.append((label, component.get_class().get_name(), _groom_asset_path(component)))
    return grooms


def require_donor_grooms() -> list[tuple[str, str, str]]:
    donor = unreal.load_asset(DONOR_BP)
    if not donor:
        raise RuntimeError(f"Could not load {DONOR_BP}")
    grooms = collect_grooms(donor)
    log(f"Donor grooms ({len(grooms)}):")
    for label, cls, path in grooms:
        log(f"  {label} ({cls}): {path}")
    if not grooms:
        raise RuntimeError(
            "BP_MH_RealityErrol has no groom components (Hair/Beard/Mustache/"
            "Eyebrows/Eyelashes). Save + Assemble MH_RealityErrol in MetaHuman "
            "Creator so grooms are on the assembled Blueprint, then re-run."
        )
    hair = [item for item in grooms if _is_hair_label(item[0])]
    if not hair:
        raise RuntimeError(
            "Grooms are present but no Hair component was found on BP_MH_RealityErrol. "
            "Confirm hair is assigned in MetaHuman Creator, Assemble, then re-run."
        )
    log(f"Hair OK: {', '.join(f'{label}={path}' for label, _cls, path in hair)}")
    return grooms


def _is_clothing_component(label: str, component) -> bool:
    cls = component.get_class().get_name()
    if "SkeletalMesh" not in cls:
        return False
    if label in SKIP_MESH_LABELS:
        return False
    if any(token.lower() in label.lower() for token in GROOM_TOKENS):
        return False
    path = _mesh_path(component)
    if path == "(none)":
        return False
    if "BodyMesh" in path or "FaceMesh" in path:
        return False
    return True


def snapshot_clothing(bp) -> list[dict[str, str]]:
    items: list[dict[str, str]] = []
    for label, component, _h, _d in wiring.iter_all_components(bp):
        if not _is_clothing_component(label, component):
            continue
        hidden = wiring._get_bool_prop(component, ("b_hidden_in_game", "bHiddenInGame"))
        items.append(
            {
                "label": label,
                "mesh": _mesh_path(component),
                "hidden": str(bool(hidden)),
            }
        )
        log(f"Clothing snapshot {label}: {items[-1]['mesh']} hidden={items[-1]['hidden']}")
    return items


def snapshot_previous_costume() -> list[dict[str, str]]:
    source_path = GODFREY_PERFORMER_BP
    if not unreal.EditorAssetLibrary.does_asset_exist(source_path):
        log("No current performer to snapshot clothing from")
        return []
    bp = unreal.load_asset(source_path)
    if not bp:
        return []
    items = snapshot_clothing(bp)
    visible = [item for item in items if item["hidden"] != "True"]
    if visible:
        log(f"Using {len(visible)} visible clothing mesh(es) from current performer")
        return visible
    if items:
        log("All current clothing components are hidden; using them anyway")
        return items
    return []


def fallback_outfit_snapshot() -> list[dict[str, str]]:
    for path in OUTFIT_FALLBACKS:
        if unreal.EditorAssetLibrary.does_asset_exist(path):
            log(f"No clothing on current performer — falling back to {path}")
            return [{"label": "SkeletalMesh", "mesh": path, "hidden": "False"}]
    raise RuntimeError(
        "Could not find a previous MHC outfit mesh to transfer "
        "(looked for MHC_CaptainGodfrey_Outfits1 / Outfits / MHC_Errol_Outfits)."
    )


def assign_clothing_mesh(comp, mesh) -> bool:
    try:
        comp.set_skeletal_mesh_asset(mesh)
        return True
    except Exception:
        pass
    return bool(wiring.set_prop(comp, ["skeletal_mesh", "SkeletalMesh", "SkinnedAsset"], mesh))


def pin_clothing_to_body(comp, body) -> None:
    wiring.set_prop(comp, ["disable_cloth_simulation", "b_disable_cloth_simulation"], True)
    wiring.set_prop(comp, ["cloth_blend_weight"], 0.0)
    wiring.set_prop(comp, ["allow_cloth_actors", "b_allow_cloth_actors"], False)
    wiring.set_prop(
        comp,
        ["visibility_based_anim_tick_option", "VisibilityBasedAnimTickOption"],
        unreal.VisibilityBasedAnimTickOption.ALWAYS_TICK_POSE_AND_REFRESH_BONES,
    )
    wiring.set_prop(comp, ["b_hidden_in_game", "bHiddenInGame"], False)
    wiring.set_prop(comp, ["visible", "bVisible"], True)
    if not body:
        return
    try:
        comp.set_leader_pose_component(body, True, True)
        log("  leader pose -> Body")
    except Exception as exc:
        log(f"  WARN leader pose: {exc}")


def add_skeletal_mesh_component(bp, desired_label: str):
    existing, _ = wiring.find_component(bp, desired_label)
    if existing:
        return existing

    component_class = unreal.load_class(None, SKEL_MESH_COMPONENT_CLASS)
    if not component_class:
        raise RuntimeError("Could not load SkeletalMeshComponent class")

    subsystem = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    if not subsystem:
        raise RuntimeError("SubobjectDataSubsystem unavailable")

    actor_handle, root_handle = wiring.find_subobject_handles(bp)
    lib = unreal.SubobjectDataBlueprintFunctionLibrary
    body_handle = None
    for handle in subsystem.k2_gather_subobject_data_for_blueprint(bp):
        data = lib.get_data(handle)
        if not data or not lib.is_component(data):
            continue
        if str(lib.get_variable_name(data)) == "Body":
            body_handle = handle
            break

    parent_handle = body_handle or root_handle or actor_handle
    if not parent_handle:
        raise RuntimeError(f"No parent handle for {desired_label}")

    params = unreal.AddNewSubobjectParams()
    for prop, value in (
        ("ParentHandle", parent_handle),
        ("parent_handle", parent_handle),
        ("NewClass", component_class),
        ("new_class", component_class),
        ("BlueprintContext", bp),
        ("blueprint_context", bp),
    ):
        try:
            params.set_editor_property(prop, value)
        except Exception:
            pass

    try:
        add_result = subsystem.add_new_subobject(params)
    except TypeError:
        fail_reason = unreal.Text()
        add_result = subsystem.add_new_subobject(params, fail_reason)

    new_handle = add_result[0] if isinstance(add_result, (tuple, list)) else add_result
    if not new_handle:
        raise RuntimeError(f"add_new_subobject failed for {desired_label}")

    for rename_args in (
        (new_handle, unreal.Text(desired_label)),
        (new_handle, desired_label),
    ):
        try:
            subsystem.rename_subobject(*rename_args)
            break
        except Exception:
            continue

    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    added, _ = wiring.find_component(bp, desired_label)
    if added:
        return added

    known = set(SKIP_MESH_LABELS)
    for label, component, _h, _d in wiring.iter_all_components(bp):
        cls = component.get_class().get_name()
        if "SkeletalMeshComponent" not in cls:
            continue
        if label in known or any(token.lower() in label.lower() for token in GROOM_TOKENS):
            continue
        if label.startswith("SkeletalMesh"):
            log(f"Using auto-named clothing component {label} for {desired_label}")
            return component
    raise RuntimeError(f"Could not find newly added clothing component {desired_label}")


def hide_default_underwear(bp) -> None:
    for label, component, _h, _d in wiring.iter_all_components(bp):
        if "SkeletalMesh" not in component.get_class().get_name():
            continue
        path = _mesh_path(component)
        if "DefaultGarment" not in path and "bodyShape" not in path:
            continue
        wiring.set_prop(component, ["b_hidden_in_game", "bHiddenInGame"], True)
        wiring.set_prop(component, ["visible", "bVisible"], False)
        log(f"Hid default garment {label}: {path}")


def apply_previous_costume(bp, clothing: list[dict[str, str]]) -> None:
    if not clothing:
        clothing = fallback_outfit_snapshot()
    body, _ = wiring.find_component(bp, "Body")
    hide_default_underwear(bp)
    assigned = 0
    for item in clothing:
        mesh = unreal.load_asset(item["mesh"].split(".")[0] if "." in item["mesh"] else item["mesh"])
        if not mesh:
            log(f"WARN: missing outfit mesh {item['mesh']}")
            continue
        label = item["label"]
        comp, _ = wiring.find_component(bp, label)
        if not comp or label in SKIP_MESH_LABELS:
            comp = None
        if not comp:
            # Reuse an existing extra skeletal mesh slot if the donor already has one.
            for existing_label, existing, _h, _d in wiring.iter_all_components(bp):
                if existing_label in SKIP_MESH_LABELS:
                    continue
                if "SkeletalMesh" not in existing.get_class().get_name():
                    continue
                if any(token.lower() in existing_label.lower() for token in GROOM_TOKENS):
                    continue
                path = _mesh_path(existing)
                if "BodyMesh" in path or "FaceMesh" in path:
                    continue
                if "DefaultGarment" in path or path == "(none)" or "Outfits" not in path:
                    comp = existing
                    label = existing_label
                    break
        if not comp:
            comp = add_skeletal_mesh_component(bp, label)
        if not assign_clothing_mesh(comp, mesh):
            raise RuntimeError(f"Failed to assign {item['mesh']} onto {label}")
        pin_clothing_to_body(comp, body)
        assigned += 1
        log(f"Costume {label} <- {item['mesh']}")
    if assigned == 0:
        raise RuntimeError("Failed to transfer previous costume onto RealityErrol performer")
    log(f"Transferred {assigned} clothing mesh(es) from previous performer")


def log_mesh_inventory(bp, title: str) -> None:
    log(f"{title} mesh/groom inventory:")
    for label, component, _h, _d in wiring.iter_all_components(bp):
        cls = component.get_class().get_name()
        if "SkeletalMesh" in cls:
            log(f"  {label} ({cls}): {_mesh_path(component)}")
        elif _is_groom_component(label, component):
            log(f"  {label} ({cls}): {_groom_asset_path(component)}")


def _close_asset_editors(asset_path: str) -> None:
    try:
        asset = unreal.load_asset(asset_path)
        if not asset:
            return
        subsystem = unreal.get_editor_subsystem(unreal.AssetEditorSubsystem)
        if subsystem:
            subsystem.close_all_editors_for_asset(asset)
    except Exception as exc:
        log(f"WARN: close editors for {asset_path}: {exc}")


def _force_delete_asset(asset_path: str) -> None:
    """Delete an asset and verify it is gone (handles soft delete / file-lock failures)."""
    if not unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        return

    _close_asset_editors(asset_path)
    unreal.EditorAssetLibrary.delete_asset(asset_path)

    if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        import time

        time.sleep(1.0)
        _close_asset_editors(asset_path)
        unreal.EditorAssetLibrary.delete_asset(asset_path)

    if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        raise RuntimeError(
            f"Could not delete {asset_path} (file locked?). Close Unreal Editor "
            f"tabs/PIE on this asset and re-run with only UnrealEditor-Cmd."
        )
    log(f"Deleted {asset_path}")


def archive_existing_godfrey_shell() -> None:
    if not unreal.EditorAssetLibrary.does_directory_exist(GODFREY_PACKAGE):
        unreal.EditorAssetLibrary.make_directory(GODFREY_PACKAGE)

    if not unreal.EditorAssetLibrary.does_asset_exist(GODFREY_PERFORMER_BP):
        log(f"No existing {GODFREY_PERFORMER_BP} to archive")
        return

    _close_asset_editors(GODFREY_PERFORMER_BP)

    # Duplicate (do not rename) so Godfrey_World soft refs keep pointing at
    # /Game/MetaHumans/Godfrey/BP_Godfrey_Performer after we recreate it.
    if not unreal.EditorAssetLibrary.does_asset_exist(ARCHIVE_BP):
        duplicated = unreal.EditorAssetLibrary.duplicate_asset(
            GODFREY_PERFORMER_BP, ARCHIVE_BP
        )
        if not duplicated and not unreal.EditorAssetLibrary.does_asset_exist(ARCHIVE_BP):
            raise RuntimeError(
                f"Failed to archive {GODFREY_PERFORMER_BP} -> {ARCHIVE_BP}"
            )
        log(f"Archived current performer shell -> {ARCHIVE_BP}")
    else:
        log(f"Archive already exists: {ARCHIVE_BP} — replacing performer shell")

    _force_delete_asset(GODFREY_PERFORMER_BP)

    if unreal.EditorAssetLibrary.does_asset_exist(GODFREY_PERFORMER_BP):
        raise RuntimeError(
            f"{GODFREY_PERFORMER_BP} still present after archive — aborting before donor duplicate"
        )


def create_reality_errol_shell() -> object:
    require_reality_errol_donor()

    if not unreal.EditorAssetLibrary.does_directory_exist(GODFREY_PACKAGE):
        unreal.EditorAssetLibrary.make_directory(GODFREY_PACKAGE)

    if unreal.EditorAssetLibrary.does_asset_exist(GODFREY_PERFORMER_BP):
        raise RuntimeError(
            f"{GODFREY_PERFORMER_BP} still exists after archive/delete — aborting"
        )

    duplicated = unreal.EditorAssetLibrary.duplicate_asset(DONOR_BP, GODFREY_PERFORMER_BP)
    if not duplicated and not unreal.EditorAssetLibrary.does_asset_exist(GODFREY_PERFORMER_BP):
        raise RuntimeError(f"Failed to duplicate {DONOR_BP} -> {GODFREY_PERFORMER_BP}")

    bp = unreal.load_asset(GODFREY_PERFORMER_BP)
    if not bp:
        raise RuntimeError(f"Could not load {GODFREY_PERFORMER_BP}")

    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    log(f"Created MHC shell {GODFREY_PERFORMER_BP} from {DONOR_BP}")
    log_mesh_inventory(bp, "New performer")
    return bp


def verify_body_shares_metahuman_skeleton() -> dict[str, str]:
    """Performance AS library uses metahuman_base_skel; RealityErrol must too."""
    log(f"Using body mesh for skeleton check: {BODY_MESH}")
    if not unreal.EditorAssetLibrary.does_asset_exist(wiring.SPEAKING_IDLE_MONTAGE_PATH):
        raise RuntimeError(
            f"Missing asset for skeleton check: {wiring.SPEAKING_IDLE_MONTAGE_PATH}"
        )

    montage = unreal.load_asset(wiring.SPEAKING_IDLE_MONTAGE_PATH)
    body_mesh = unreal.load_asset(BODY_MESH)
    montage_skel = wiring._object_skeleton(montage)
    body_skel = wiring._object_skeleton(body_mesh)
    if not montage_skel or not body_skel:
        raise RuntimeError("Montage or RealityErrol body has no skeleton")

    montage_path = montage_skel.get_path_name()
    body_path = body_skel.get_path_name()
    compatible = (
        montage_skel == body_skel
        or montage_path == body_path
        or ("metahuman_base_skel" in montage_path and "metahuman_base_skel" in body_path)
    )
    result = {
        "montage_skeleton": montage_path,
        "mhc_body_skeleton": body_path,
        "compatible": str(compatible),
    }
    if not compatible:
        log(
            f"WARN: speaking montage skeleton may not match RealityErrol body "
            f"({montage_path} vs {body_path}) — lip sync still works; "
            "leave bEnableBodyMontages=false until retarget"
        )
    else:
        log(f"Speaking montage skeleton compatible with RealityErrol body ({body_path})")
    return result


def wire_exhibition_stack(bp) -> None:
    log("Adding exhibition components…")
    wiring.add_component_to_blueprint(
        bp, wiring.BRIDGE_LABEL, wiring.BRIDGE_CLASS, "actor"
    )
    wiring.add_ace_curve_source(bp)
    wiring.add_performance_state(bp)
    wiring.add_ace_warmup(bp)

    if wiring.find_component_by_class(bp, "GodfreyDirectSpeechComponent")[0]:
        wiring.remove_direct_speech(bp)
        log("Removed GodfreyDirectSpeech (exhibition uses GM queue poll)")

    log("Configuring Body AnimInstance + Face_AnimBP (ACE)…")
    for key, value in wiring.assign_body_anim_instance(bp).items():
        log(f"Body.{key} = {value}")
    for key, value in wiring.ensure_face_anim_bp(bp).items():
        log(f"Face.{key} = {value}")

    log("Configuring bridge / ACE / performance (RealityErrol, garments OFF, montages ON)…")
    for key, value in wiring.configure_inert_bridge(bp).items():
        log(f"Bridge.{key} = {value}")

    skel = verify_body_shares_metahuman_skeleton()
    for key, value in skel.items():
        log(f"skeleton.{key} = {value}")

    if unreal.EditorAssetLibrary.does_asset_exist(wiring.SPEAKING_IDLE_MONTAGE_PATH):
        for key, value in wiring.configure_speaking_idle_montage(
            bp, reset_idle_micro=False
        ).items():
            log(f"Bridge.montage.{key} = {value}")
    else:
        log(f"WARN: missing {wiring.SPEAKING_IDLE_MONTAGE_PATH} — skipping montage assign")

    for key, value in wiring.configure_body_for_montage_playback(bp).items():
        log(f"Body.playback.{key} = {value}")

    for key, value in wiring.configure_active_ace(bp).items():
        log(f"ACE.{key} = {value}")
    for key, value in wiring.configure_ace_warmup(bp).items():
        log(f"AceWarmup.{key} = {value}")

    state, _ = wiring.find_component_by_class(bp, "GodfreyPerformanceStateComponent")
    if not state:
        raise RuntimeError("PerformanceState missing after add")
    for names, value, key in (
        (["b_auto_activate", "bAutoActivate"], True, "bAutoActivate"),
        (
            ["b_auto_speaking_state_from_utterance", "bAutoSpeakingStateFromUtterance"],
            True,
            "bAutoSpeakingStateFromUtterance",
        ),
        (
            ["b_route_performance_cues_to_states", "bRoutePerformanceCuesToStates"],
            True,
            "bRoutePerformanceCuesToStates",
        ),
    ):
        if wiring.set_prop(state, names, value):
            log(f"PerformanceState.{key} = {value}")

    for key, value in wiring.configure_idle_micro_motion(bp).items():
        log(f"Bridge.idle.{key} = {value}")

    bridge, _ = wiring.find_component_by_class(
        bp, "GodfreyPerformerAnimationBridgeComponent"
    )
    if bridge:
        for names, value, key in (
            (
                ["b_manage_meta_human_garments_at_runtime", "bManageMetaHumanGarmentsAtRuntime"],
                False,
                "bManageMetaHumanGarmentsAtRuntime",
            ),
            (
                ["b_auto_wire_clothing_leader_pose_to_body", "bAutoWireClothingLeaderPoseToBody"],
                False,
                "bAutoWireClothingLeaderPoseToBody",
            ),
            (["b_enable_body_montages", "bEnableBodyMontages"], True, "bEnableBodyMontages"),
            (["b_auto_activate", "bAutoActivate"], True, "bAutoActivate"),
        ):
            if wiring.set_prop(bridge, names, value):
                log(f"Bridge.safety.{key} = {value}")


def audit_migrated_performer(bp) -> dict[str, object]:
    labels = _component_labels(bp)
    log(f"Components: {', '.join(labels)}")

    required = (
        "GodfreyPerformerAnimationBridgeComponent",
        "ACEAudioCurveSourceComponent",
        "GodfreyPerformanceStateComponent",
        "GodfreyAceWarmupComponent",
    )
    for token in required:
        if not wiring.find_component_by_class(bp, token)[0]:
            raise RuntimeError(f"Missing required component class containing {token}")

    if wiring.find_component_by_class(bp, "GodfreyDirectSpeechComponent")[0]:
        raise RuntimeError("DirectSpeech must not be on performer (queue poll is on GM)")

    body_comp, _ = wiring.find_component(bp, "Body")
    face_comp, _ = wiring.find_component(bp, "Face")
    if not body_comp or not face_comp:
        raise RuntimeError("MHC shell must retain Body and Face mesh components")

    body_path = _mesh_path(body_comp)
    face_path = _mesh_path(face_comp)
    log(f"Body mesh: {body_path}")
    log(f"Face mesh: {face_path}")
    if "RealityErrol" not in body_path:
        log(f"WARN: Body mesh is not SKM_MH_RealityErrol_* ({body_path})")
    if "RealityErrol" not in face_path:
        log(f"WARN: Face mesh is not SKM_MH_RealityErrol_* ({face_path})")

    body_anim = wiring._anim_class_name(body_comp)
    face_anim = wiring._anim_class_name(face_comp)
    if "GodfreyBodyAnimInstance" not in body_anim:
        raise RuntimeError(
            f"Body AnimClass must be GodfreyBodyAnimInstance (got {body_anim})"
        )
    if "Face_AnimBP" not in face_anim:
        raise RuntimeError(
            f"Face AnimClass must be Face_AnimBP for ACE lip sync (got {face_anim})"
        )

    ace_audit = wiring.audit_ace_active(bp)
    warmup, _ = wiring.find_component_by_class(bp, "GodfreyAceWarmupComponent")
    if wiring._get_bool_prop(warmup, ("b_warmup_on_begin_play", "bWarmupOnBeginPlay")) is not True:
        raise RuntimeError("AceWarmup bWarmupOnBeginPlay must be True")

    state, _ = wiring.find_component_by_class(bp, "GodfreyPerformanceStateComponent")
    if wiring._get_bool_prop(
        state, ("b_auto_speaking_state_from_utterance", "bAutoSpeakingStateFromUtterance")
    ) is not True:
        raise RuntimeError("bAutoSpeakingStateFromUtterance must be True")
    if wiring._get_bool_prop(
        state, ("b_route_performance_cues_to_states", "bRoutePerformanceCuesToStates")
    ) is not True:
        raise RuntimeError("bRoutePerformanceCuesToStates must be True")

    bridge, _ = wiring.find_component_by_class(
        bp, "GodfreyPerformerAnimationBridgeComponent"
    )
    if wiring._get_bool_prop(
        bridge,
        ("b_manage_meta_human_garments_at_runtime", "bManageMetaHumanGarmentsAtRuntime"),
    ) is not False:
        raise RuntimeError("bManageMetaHumanGarmentsAtRuntime must stay False on MHC")
    if wiring._get_bool_prop(bridge, ("b_enable_body_montages", "bEnableBodyMontages")) is not True:
        raise RuntimeError("bEnableBodyMontages must be True for AS_* performance library")

    grooms = collect_grooms(bp)
    hair = [item for item in grooms if _is_hair_label(item[0])]
    if not hair:
        raise RuntimeError("Migrated performer lost Hair grooms")
    clothing = snapshot_clothing(bp)
    visible_outfits = [
        item
        for item in clothing
        if item["hidden"] != "True"
        and (
            "Outfits" in item["mesh"]
            or "Cinched" in item["mesh"]
            or "Costume" in item["mesh"]
            or "casual_formal" in item["mesh"]
        )
    ]
    if not visible_outfits:
        raise RuntimeError("Migrated performer has no visible transferred clothing mesh")

    return {
        "body_anim": body_anim,
        "face_anim": face_anim,
        "body_mesh": body_path,
        "face_mesh": face_path,
        "hair": "; ".join(f"{label}={path}" for label, _cls, path in hair),
        "groom_count": len(grooms),
        "clothing": "; ".join(f"{item['label']}={item['mesh']}" for item in visible_outfits),
        "ace_active": ace_audit.get("ace_bAutoActivate"),
        "garments_managed": False,
        "body_montages": True,
        "direct_speech_absent": True,
    }


def audit_gamemode_queue() -> None:
    gm = unreal.load_asset(GM_EXHIBIT)
    if not gm:
        raise RuntimeError(f"Missing GameMode {GM_EXHIBIT}")
    poll_audit = wiring.audit_gamemode_exhibition_queue(gm)
    for key, value in poll_audit.items():
        log(f"GM.audit.{key} = {value}")


def main() -> None:
    log("=== MH_RealityErrol -> BP_Godfrey_Performer migration ===")
    project_dir = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
    log(f"Project dir: {project_dir}")
    if "MetaHuman_Baseline_UE58_Test" not in project_dir.replace("\\", "/"):
        raise RuntimeError(
            f"Project did not load (got {project_dir!r}). "
            "Relaunch UnrealEditor-Cmd with a quoted .uproject path "
            '(spaces in "UE Projects" break unquoted args).'
        )
    wait_for_asset_registry()
    require_reality_errol_donor()
    require_donor_grooms()
    clothing = snapshot_previous_costume()
    if not clothing:
        clothing = fallback_outfit_snapshot()
    archive_existing_godfrey_shell()
    bp = create_reality_errol_shell()
    wire_exhibition_stack(bp)
    apply_previous_costume(bp, clothing)
    log_mesh_inventory(bp, "Performer after costume transfer")

    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    audit = audit_migrated_performer(bp)
    for key, value in audit.items():
        log(f"performer.audit.{key} = {value}")

    audit_gamemode_queue()

    if not wiring.save_godfrey_performer_blueprint(bp):
        raise RuntimeError(wiring.BP_SAVE_LOCK_HINT)

    if unreal.EditorAssetLibrary.does_asset_exist(ARCHIVE_BP):
        unreal.EditorAssetLibrary.save_asset(ARCHIVE_BP, only_if_is_dirty=False)

    write_report(True)
    log(
        "PASS — BP_Godfrey_Performer is now MH_RealityErrol + previous MHC costume + ACE stack. "
        "Open Godfrey_World (load exhibit cells) so the placed actor picks up the new shell. "
        "PIE: confirm hair/grooms + coat, then queue TTS → voice + lip sync."
    )


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        unreal.log_error(f"[MigrateRealityErrol] {exc}")
        write_report(False)
        sys.exit(1)
