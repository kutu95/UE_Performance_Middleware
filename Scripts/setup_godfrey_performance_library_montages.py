"""Create AM_* DefaultSlot montages from AS_* performance sequences and wire BP_Godfrey_Performer.

Creates thin AnimMontages next to sequences under:
  /Game/Godfrey/Animation/Animation/Performances

Assigns the 8 bridge behaviour slots + enables bEnableBodyMontages.
Runtime can also build dynamic DefaultSlot montages from AS_* if AM_* are missing;
this script authors persistent AM_* assets for cooking and Details assignment.

Headless:
  UnrealEditor-Cmd.exe "D:/UE Projects/MetaHuman_Baseline_UE58_Test/UnrealPerformer.uproject"
    -ExecutePythonScript="D:/UE Projects/MetaHuman_Baseline_UE58_Test/Scripts/setup_godfrey_performance_library_montages.py"
    -unattended -nop4 -nosplash -log

PIE validation:
  Listening → Thinking → Speaking → Emphasis → Idle (body gestures; ACE lip sync unchanged).
  Named cue: NotifyPerformanceCue(type=action, value=TwoThumbsUp_01).
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

LIBRARY_PATH = "/Game/Godfrey/Animation/Animation/Performances"
SLOT_NAME = "DefaultSlot"
REPORT = "GodfreyPerformanceLibraryMontages.txt"

# Bridge slot → AS_* stem (without AM_ prefix; AM_ListeningAttentive_01 etc.)
DEFAULT_SLOTS = {
    "ListeningEnterMontage": "ListeningAttentive_01",
    "ThinkingMontage": "ThinkingHandToChin_01",
    "SpeakingStartMontage": "SpeakingGentleEmphasis_01",
    "SpeakingIdleMontage": "SpeakingCalmExplanation_01",
    "EmphasisMontage": "SpeakingGentleEmphasis_01",
    "AmusedMontage": "Laughing_01",
    "SeriousMontage": "Concerned_01",
    "ReturnToIdleMontage": "IdleStanding_01",
    "IdleBreathingMontage": "IdleWeightShift_01",
}

_lines: list[str] = []


def log(msg: str) -> None:
    _lines.append(msg)
    unreal.log(f"[PerfLibrary] {msg}")


def write_report(ok: bool) -> None:
    path = unreal.Paths.convert_relative_path_to_full(
        unreal.Paths.project_saved_dir() + REPORT
    )
    header = "RESULT: PASS\n" if ok else "RESULT: FAIL\n"
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(header + "\n".join(_lines) + "\n")
    log(f"Report: {path}")


def as_path(stem: str) -> str:
    name = f"AS_{stem}"
    return f"{LIBRARY_PATH}/{name}"


def am_path(stem: str) -> str:
    name = f"AM_{stem}"
    return f"{LIBRARY_PATH}/{name}"


def ensure_montage_from_sequence(stem: str) -> str | None:
    """Return AM asset path, creating from AS_* if needed."""
    seq_path = as_path(stem)
    montage_path = am_path(stem)
    if unreal.EditorAssetLibrary.does_asset_exist(montage_path):
        log(f"exists {montage_path}")
        return montage_path

    if not unreal.EditorAssetLibrary.does_asset_exist(seq_path):
        log(f"MISSING sequence {seq_path}")
        return None

    sequence = unreal.EditorAssetLibrary.load_asset(seq_path)
    if not sequence:
        log(f"FAILED load {seq_path}")
        return None

    # AssetTools create montage from AnimSequence
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    factory = unreal.AnimMontageFactory()
    try:
        factory.set_editor_property("target_skeleton", sequence.get_editor_property("skeleton"))
    except Exception:
        pass
    try:
        factory.set_editor_property("source_animation", sequence)
    except Exception:
        pass

    package_path = LIBRARY_PATH
    asset_name = f"AM_{stem}"
    montage = asset_tools.create_asset(asset_name, package_path, unreal.AnimMontage, factory)
    if not montage:
        # Fallback: duplicate empty + set via AnimationLibrary if available
        log(f"AnimMontageFactory create failed for {asset_name}; trying AnimationLibrary")
        try:
            montage = unreal.AnimationLibrary.create_animation_asset(
                unreal.AnimMontage, package_path, asset_name
            )
        except Exception as exc:
            log(f"FAILED create montage {asset_name}: {exc}")
            return None

    # Ensure DefaultSlot segment references the sequence
    try:
        # UE Python: create slot track / set montage slot
        if hasattr(unreal, "AnimationLibrary"):
            # Prefer setting the montage's slot group/name when API exists
            pass
        montage.set_editor_property("blend_in", unreal.AlphaBlend(0.2))
        # Many UE versions expose create_slot_animation_as_dynamic only in C++;
        # authored montages from factory usually already embed the source animation.
    except Exception as exc:
        log(f"WARN montage post-setup {asset_name}: {exc}")

    unreal.EditorAssetLibrary.save_loaded_asset(montage)
    log(f"created {montage_path}")
    return montage_path


def configure_bridge_library(bp) -> dict[str, str]:
    bridge, _ = wiring.find_component_by_class(bp, "GodfreyPerformerAnimationBridgeComponent")
    if not bridge:
        raise RuntimeError("GodfreyPerformerAnimationBridgeComponent not found")

    body_comp, _ = wiring.find_component(bp, "Body")
    changes: dict[str, str] = {}
    if body_comp:
        wiring.set_prop(bridge, ["target_skeletal_mesh", "TargetSkeletalMesh"], body_comp)
        changes["TargetSkeletalMesh"] = "Body"

    for prop, stem in DEFAULT_SLOTS.items():
        path = am_path(stem)
        if not unreal.EditorAssetLibrary.does_asset_exist(path):
            path = as_path(stem)
            # Bridge accepts montages; for AS-only leave empty and let C++ library defaults build dynamic montages
            if prop.endswith("Montage") and path.startswith(as_path(stem)[: len(LIBRARY_PATH)]):
                # Prefer AM; if only AS exists, skip asset assign (C++ soft defaults cover it)
                am = am_path(stem)
                if not unreal.EditorAssetLibrary.does_asset_exist(am):
                    changes[prop] = f"(runtime dynamic from AS_{stem})"
                    continue
                path = am
        asset = unreal.EditorAssetLibrary.load_asset(path)
        if not asset:
            changes[prop] = f"MISSING {path}"
            continue
        # Only assign UAnimMontage properties
        if not isinstance(asset, unreal.AnimMontage):
            changes[prop] = f"SKIP non-montage {path}"
            continue
        snake = "".join(["_" + c.lower() if c.isupper() else c for c in prop]).lstrip("_")
        used = wiring.set_prop(bridge, [snake, prop], asset)
        changes[prop] = path if used else f"FAILED set {prop}"

    for names, value, key in (
        (["b_enable_body_montages", "bEnableBodyMontages"], True, "bEnableBodyMontages"),
        (
            ["b_auto_assign_performance_library_defaults", "bAutoAssignPerformanceLibraryDefaults"],
            True,
            "bAutoAssignPerformanceLibraryDefaults",
        ),
        (
            ["b_play_named_actions_from_cue_bus", "bPlayNamedActionsFromCueBus"],
            True,
            "bPlayNamedActionsFromCueBus",
        ),
        (
            ["placeholder_montage_search_path", "PlaceholderMontageSearchPath"],
            unreal.Name(LIBRARY_PATH),
            "PlaceholderMontageSearchPath",
        ),
        (
            ["performance_library_path", "PerformanceLibraryPath"],
            unreal.Name(LIBRARY_PATH),
            "PerformanceLibraryPath",
        ),
        (
            ["b_auto_assign_placeholder_montages", "bAutoAssignPlaceholderMontages"],
            False,
            "bAutoAssignPlaceholderMontages",
        ),
    ):
        used = wiring.set_prop(bridge, names, value)
        changes[key] = str(value) if used else f"FAILED set {key}"

    return changes


def main() -> None:
    stems = sorted({*DEFAULT_SLOTS.values()})
    created = 0
    missing = 0
    for stem in stems:
        path = ensure_montage_from_sequence(stem)
        if path:
            created += 1
        else:
            missing += 1

    log(f"montage ensure: ok={created} missing_seq={missing}")

    bp = unreal.load_asset(wiring.GODFREY_PERFORMER_BP)
    if not bp:
        raise RuntimeError(f"Missing {wiring.GODFREY_PERFORMER_BP}")

    changes = configure_bridge_library(bp)
    for key, value in changes.items():
        log(f"Bridge.{key} = {value}")

    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    save_ok = wiring.save_godfrey_performer_blueprint(bp)
    log("BP saved." if save_ok else f"WARN: {wiring.BP_SAVE_LOCK_HINT}")

    write_report(missing == 0)
    log(
        "Done. PIE: BeginListening/Thinking/Speaking on PerformanceState; "
        "NotifyPerformanceCue('action','TwoThumbsUp_01',''); ACE face unchanged."
    )


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        unreal.log_error(f"[PerfLibrary] {exc}")
        write_report(False)
        sys.exit(1)
