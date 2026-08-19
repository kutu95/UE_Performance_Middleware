"""Replace BP_Godfrey_Performer with assembled MHC_Errol (Errol likeness) and re-wire
the exhibition ACE / lip-sync / performance stack.

Keeps soft path /Game/MetaHumans/Godfrey/BP_Godfrey_Performer so Godfrey_World labels,
queue poll fallbacks, and validators keep working.

Prerequisite: Bind MH_ID_Errol on MHC_Errol in MetaHuman Creator → Assemble so this exists:
  /Game/MetaHumans/MHC_Errol/BP_MHC_Errol
  /Game/MetaHumans/MHC_Errol/Body/SKM_MHC_Errol_BodyMesh
  /Game/MetaHumans/MHC_Errol/Face/SKM_MHC_Errol_FaceMesh

Does NOT spawn/destroy level actors (World Partition — do level swap in editor).

Headless:
  UnrealEditor-Cmd.exe "D:/UE Projects/MetaHuman_Baseline_UE58_Test/UnrealPerformer.uproject"
    -ExecutePythonScript="D:/UE Projects/MetaHuman_Baseline_UE58_Test/Scripts/migrate_errol_to_godfrey_performer.py"
    -unattended -nop4 -nosplash -log

After PASS: open Godfrey_World in editor and run
  Scripts/swap_godfrey_world_to_mhc_performer.py
  (Tools → Execute Python Script) to replace the level actor if still on the old shell.
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

# Assembled Errol likeness package (not MHC_CaptainGodfrey stylization).
DONOR_PACKAGE = "/Game/MetaHumans/MHC_Errol"
DONOR_BP = f"{DONOR_PACKAGE}/BP_MHC_Errol"
# Prefer Errol body mesh; never fall back to CaptainGodfrey for this migration.
ERROL_BODY_MESH_CANDIDATES = (
    f"{DONOR_PACKAGE}/Body/SKM_MHC_Errol_BodyMesh",
)
ERROL_FACE_MESH = f"{DONOR_PACKAGE}/Face/SKM_MHC_Errol_FaceMesh"
GODFREY_PACKAGE = "/Game/MetaHumans/Godfrey"
GODFREY_PERFORMER_BP = wiring.GODFREY_PERFORMER_BP
# Archive the current Captain-backed performer (Kristofer archive stays untouched).
ARCHIVE_BP = "/Game/MetaHumans/Godfrey/BP_Godfrey_Performer_CaptainGodfrey_Archive"
GM_EXHIBIT = wiring.GM_GODFREY_EXHIBIT

REPORT = "MigrateErrolLikenessToPerformer.txt"
_lines: list[str] = []


def log(msg: str) -> None:
    _lines.append(msg)
    unreal.log(f"[MigrateErrolLikeness] {msg}")


def write_report(ok: bool) -> None:
    # Prefer project Saved/; fall back to absolute path if engine started without a game.
    saved = unreal.Paths.project_saved_dir()
    path = unreal.Paths.convert_relative_path_to_full(saved + REPORT)
    if "UnrealEngine" in path.replace("\\", "/") and "MetaHuman_Baseline" not in path:
        path = r"D:\UE Projects\MetaHuman_Baseline_UE58_Test\Saved\\" + REPORT
    header = "RESULT: PASS\n" if ok else "RESULT: FAIL\n"
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(header + "\n".join(_lines) + "\n")
    log(f"Report: {path}")


def resolve_errol_body_mesh() -> str:
    for path in ERROL_BODY_MESH_CANDIDATES:
        if unreal.EditorAssetLibrary.does_asset_exist(path):
            return path
    raise RuntimeError(
        "Missing Errol body mesh. Expected one of:\n  "
        + "\n  ".join(ERROL_BODY_MESH_CANDIDATES)
        + "\nAssemble MHC_Errol with MH_ID_Errol bound so SKM_MHC_Errol_BodyMesh is written."
    )


def require_errol_donor_bp() -> None:
    if not unreal.EditorAssetLibrary.does_asset_exist(DONOR_BP):
        raise RuntimeError(
            f"Missing assembled donor {DONOR_BP}.\n"
            "Open MHC_Errol in MetaHuman Creator → bind MH_ID_Errol → Assembly → Assemble "
            f"(output / name MHC_Errol) so Content/MetaHumans/MHC_Errol/ "
            "has BP_MHC_Errol and SKM_MHC_Errol_* meshes, then re-run this script."
        )
    if not unreal.EditorAssetLibrary.does_asset_exist(ERROL_FACE_MESH):
        raise RuntimeError(
            f"Missing {ERROL_FACE_MESH}. Assemble MHC_Errol after binding MH_ID_Errol "
            "(do not migrate while Body/Face still point at SKM_MHC_CaptainGodfrey_*)."
        )
    resolve_errol_body_mesh()
    log(f"Donor OK: {DONOR_BP} + Errol body/face meshes")


def wait_for_asset_registry() -> None:
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    # Block until on-disk scan finishes so does_asset_exist is reliable in -ExecutePythonScript.
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
        # Retry once after a short settle — common when another editor held the package.
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

    if not unreal.EditorAssetLibrary.does_asset_exist(ARCHIVE_BP):
        # Prefer rename so we do not need a second copy while the source is locked.
        renamed = unreal.EditorAssetLibrary.rename_asset(GODFREY_PERFORMER_BP, ARCHIVE_BP)
        if renamed or unreal.EditorAssetLibrary.does_asset_exist(ARCHIVE_BP):
            log(f"Renamed Captain performer shell -> {ARCHIVE_BP}")
        else:
            duplicated = unreal.EditorAssetLibrary.duplicate_asset(GODFREY_PERFORMER_BP, ARCHIVE_BP)
            if not duplicated and not unreal.EditorAssetLibrary.does_asset_exist(ARCHIVE_BP):
                raise RuntimeError(f"Failed to archive {GODFREY_PERFORMER_BP} -> {ARCHIVE_BP}")
            log(f"Archived Captain performer shell (duplicate) -> {ARCHIVE_BP}")
    else:
        log(f"Archive already exists: {ARCHIVE_BP} — replacing performer shell")

    # rename_asset often leaves a redirector at the old path; delete it before MHC duplicate.
    if unreal.EditorAssetLibrary.does_asset_exist(GODFREY_PERFORMER_BP):
        _force_delete_asset(GODFREY_PERFORMER_BP)

    if unreal.EditorAssetLibrary.does_asset_exist(GODFREY_PERFORMER_BP):
        raise RuntimeError(
            f"{GODFREY_PERFORMER_BP} still present after archive — aborting before MHC duplicate"
        )


def create_mhc_godfrey_shell() -> object:
    require_errol_donor_bp()

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
    return bp

def verify_mhc_body_shares_metahuman_skeleton() -> dict[str, str]:
    """Performance AS library uses metahuman_base_skel; MHC Errol must too."""
    body_mesh_path = resolve_errol_body_mesh()
    log(f"Using body mesh for skeleton check: {body_mesh_path}")
    if not unreal.EditorAssetLibrary.does_asset_exist(wiring.SPEAKING_IDLE_MONTAGE_PATH):
        raise RuntimeError(f"Missing asset for skeleton check: {wiring.SPEAKING_IDLE_MONTAGE_PATH}")

    montage = unreal.load_asset(wiring.SPEAKING_IDLE_MONTAGE_PATH)
    body_mesh = unreal.load_asset(body_mesh_path)
    montage_skel = wiring._object_skeleton(montage)
    body_skel = wiring._object_skeleton(body_mesh)
    if not montage_skel or not body_skel:
        raise RuntimeError("Montage or MHC body has no skeleton")

    montage_path = montage_skel.get_path_name()
    body_path = body_skel.get_path_name()
    compatible = (
        montage_skel == body_skel
        or montage_path == body_path
        or "metahuman_base_skel" in montage_path
        and "metahuman_base_skel" in body_path
    )
    result = {
        "montage_skeleton": montage_path,
        "mhc_body_skeleton": body_path,
        "compatible": str(compatible),
    }
    if not compatible:
        log(
            f"WARN: speaking montage skeleton may not match MHC body "
            f"({montage_path} vs {body_path}) — lip sync still works; "
            "leave bEnableBodyMontages=false until retarget"
        )
    else:
        log(f"Speaking montage skeleton compatible with MHC body ({body_path})")
    return result


def wire_exhibition_stack(bp) -> None:
    log("Adding exhibition components…")
    wiring.add_component_to_blueprint(
        bp, wiring.BRIDGE_LABEL, wiring.BRIDGE_CLASS, "actor"
    )
    wiring.add_ace_curve_source(bp)
    wiring.add_performance_state(bp)
    wiring.add_ace_warmup(bp)

    # Ensure DirectSpeech is not on the MHC shell (queue poll owns speech on GM).
    if wiring.find_component_by_class(bp, "GodfreyDirectSpeechComponent")[0]:
        wiring.remove_direct_speech(bp)
        log("Removed GodfreyDirectSpeech (exhibition uses GM queue poll)")

    log("Configuring Body AnimInstance + Face_AnimBP (ACE)…")
    for key, value in wiring.assign_body_anim_instance(bp).items():
        log(f"Body.{key} = {value}")
    for key, value in wiring.ensure_face_anim_bp(bp).items():
        log(f"Face.{key} = {value}")

    log("Configuring bridge / ACE / performance (Errol likeness, garments OFF, montages ON)…")
    for key, value in wiring.configure_inert_bridge(bp).items():
        log(f"Bridge.{key} = {value}")

    # Body montages ON for AS_* library; assign montage asset.
    skel = verify_mhc_body_shares_metahuman_skeleton()
    for key, value in skel.items():
        log(f"skeleton.{key} = {value}")

    if unreal.EditorAssetLibrary.does_asset_exist(wiring.SPEAKING_IDLE_MONTAGE_PATH):
        for key, value in wiring.configure_speaking_idle_montage(bp, reset_idle_micro=False).items():
            log(f"Bridge.montage.{key} = {value}")
    else:
        log(f"WARN: missing {wiring.SPEAKING_IDLE_MONTAGE_PATH} — skipping montage assign")

    for key, value in wiring.configure_body_for_montage_playback(bp).items():
        log(f"Body.playback.{key} = {value}")

    for key, value in wiring.configure_active_ace(bp).items():
        log(f"ACE.{key} = {value}")
    for key, value in wiring.configure_ace_warmup(bp).items():
        log(f"AceWarmup.{key} = {value}")

    # Performance state: utterance + cue routing ON (Phase 8).
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

    # Explicit garment safety + body montages ON (matches live exhibit).
    bridge, _ = wiring.find_component_by_class(bp, "GodfreyPerformerAnimationBridgeComponent")
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

    body_anim = wiring._anim_class_name(body_comp)
    face_anim = wiring._anim_class_name(face_comp)
    if "GodfreyBodyAnimInstance" not in body_anim:
        raise RuntimeError(f"Body AnimClass must be GodfreyBodyAnimInstance (got {body_anim})")
    if "Face_AnimBP" not in face_anim:
        raise RuntimeError(f"Face AnimClass must be Face_AnimBP for ACE lip sync (got {face_anim})")

    ace_audit = wiring.audit_ace_active(bp)
    warmup, _ = wiring.find_component_by_class(bp, "GodfreyAceWarmupComponent")
    if wiring._get_bool_prop(warmup, ("b_warmup_on_begin_play", "bWarmupOnBeginPlay")) is not True:
        raise RuntimeError("AceWarmup bWarmupOnBeginPlay must be True")

    state, _ = wiring.find_component_by_class(bp, "GodfreyPerformanceStateComponent")
    if wiring._get_bool_prop(state, ("b_auto_speaking_state_from_utterance", "bAutoSpeakingStateFromUtterance")) is not True:
        raise RuntimeError("bAutoSpeakingStateFromUtterance must be True")
    if wiring._get_bool_prop(state, ("b_route_performance_cues_to_states", "bRoutePerformanceCuesToStates")) is not True:
        raise RuntimeError("bRoutePerformanceCuesToStates must be True")

    bridge, _ = wiring.find_component_by_class(bp, "GodfreyPerformerAnimationBridgeComponent")
    if wiring._get_bool_prop(bridge, ("b_manage_meta_human_garments_at_runtime", "bManageMetaHumanGarmentsAtRuntime")) is not False:
        raise RuntimeError("bManageMetaHumanGarmentsAtRuntime must stay False on MHC")
    if wiring._get_bool_prop(bridge, ("b_enable_body_montages", "bEnableBodyMontages")) is not True:
        raise RuntimeError("bEnableBodyMontages must be True for AS_* performance library")

    return {
        "body_anim": body_anim,
        "face_anim": face_anim,
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
    log("=== MHC Errol likeness -> BP_Godfrey_Performer migration ===")
    project_dir = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
    log(f"Project dir: {project_dir}")
    if "MetaHuman_Baseline_UE58_Test" not in project_dir.replace("\\", "/"):
        raise RuntimeError(
            f"Project did not load (got {project_dir!r}). "
            "Relaunch UnrealEditor-Cmd with a quoted .uproject path "
            '(spaces in "UE Projects" break unquoted args).'
        )
    wait_for_asset_registry()
    require_errol_donor_bp()
    archive_existing_godfrey_shell()
    bp = create_mhc_godfrey_shell()
    wire_exhibition_stack(bp)

    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    audit = audit_migrated_performer(bp)
    for key, value in audit.items():
        log(f"performer.audit.{key} = {value}")

    audit_gamemode_queue()

    if not wiring.save_godfrey_performer_blueprint(bp):
        raise RuntimeError(wiring.BP_SAVE_LOCK_HINT)

    # Persist archive if newly created.
    if unreal.EditorAssetLibrary.does_asset_exist(ARCHIVE_BP):
        unreal.EditorAssetLibrary.save_asset(ARCHIVE_BP, only_if_is_dirty=False)

    write_report(True)
    log(
        "PASS — BP_Godfrey_Performer is now MHC Errol likeness + ACE exhibition stack. "
        "Next: open Godfrey_World in editor and run swap_godfrey_world_to_mhc_performer.py "
        "if the placed actor is still the Captain/Bridge instance. PIE: queue TTS → voice + lip sync."
    )


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        unreal.log_error(f"[MigrateErrolLikeness] {exc}")
        write_report(False)
        sys.exit(1)
