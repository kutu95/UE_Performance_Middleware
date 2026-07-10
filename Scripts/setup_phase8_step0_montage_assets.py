"""Phase 8 step 0 — Godfrey body montage assets + SpeakingIdleMontage on bridge.

Copies (filesystem, run once before this script if assets missing):
  Test_Live_Audio/Content/Godfrey/Animation  ->  baseline/Content/Godfrey/Animation

Assets:
  - Retargeted/As_Godfrey_Talking_Anim (AnimSequence, Mixamo -> m_med_nrw)
  - Retargeted/As_Godfrey_Talking_Anim_Montage (speaking idle loop)

Wires SpeakingIdleMontage on BP_Godfrey_Performer. Does NOT enable speech-driven
body state yet (step 1). Bridge bAutoActivate stays false (no garment hacks).

Headless:
  UnrealEditor-Cmd.exe "D:/UE Projects/MetaHuman_Baseline_Test/UnrealPerformer.uproject"
    -ExecutePythonScript="D:/UE Projects/MetaHuman_Baseline_Test/Scripts/setup_phase8_step0_montage_assets.py"
    -unattended -nop4 -nosplash -log

Manual montage test (PIE): call BeginSpeaking on GodfreyPerformanceStateComponent
(console / Blueprint) — upper body should loop talking gesture. Lip sync unchanged.
"""
from __future__ import annotations

import os
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

import unreal

import godfrey_blueprint_wiring as wiring  # noqa: E402

REPORT = "Phase8Step0MontageAssets.txt"
_lines: list[str] = []


def log(msg: str) -> None:
    _lines.append(msg)
    unreal.log(f"[Phase8Step0] {msg}")


def write_report(ok: bool) -> None:
    path = unreal.Paths.convert_relative_path_to_full(
        unreal.Paths.project_saved_dir() + REPORT
    )
    header = "RESULT: PASS\n" if ok else "RESULT: FAIL\n"
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(header + "\n".join(_lines) + "\n")
    log(f"Report: {path}")


def main() -> None:
    skel_audit = wiring.verify_kristofer_speaking_montage_skeleton()
    for key, value in skel_audit.items():
        log(f"skeleton.{key} = {value}")

    bp = unreal.load_asset(wiring.GODFREY_PERFORMER_BP)
    if not bp:
        raise RuntimeError(f"Missing {wiring.GODFREY_PERFORMER_BP}")

    bridge_changes = wiring.configure_speaking_idle_montage(bp)
    for key, value in bridge_changes.items():
        log(f"Bridge.{key} = {value}")

    body_changes = wiring.configure_body_for_montage_playback(bp)
    for key, value in body_changes.items():
        log(f"Body.{key} = {value}")

    wiring.configure_inert_bridge(bp)
    wiring.configure_performance_state(bp)
    wiring.configure_active_ace(bp)
    wiring.configure_ace_warmup(bp)

    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    audit = wiring.audit_step8_speaking_montage_wired(bp)
    for key, value in audit.items():
        log(f"performer.audit.{key} = {value}")

    unreal.EditorAssetLibrary.save_loaded_assets([bp])
    unreal.EditorAssetLibrary.save_asset(wiring.GODFREY_PERFORMER_BP, only_if_is_dirty=False)
    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    write_report(True)
    log(
        "Phase 8 step 0 complete — PIE: call BeginSpeaking to preview montage; "
        "queue speech still lip-sync only until step 1"
    )


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        unreal.log_error(f"[Phase8Step0] {exc}")
        write_report(False)
        sys.exit(1)
