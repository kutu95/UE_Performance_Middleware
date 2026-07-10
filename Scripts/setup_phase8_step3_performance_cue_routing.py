"""Phase 8 step 3 — Brain performance cue routing on performance state.

Enables bRoutePerformanceCuesToStates on GodfreyPerformanceStateComponent so
NotifyPerformanceCue from the speech stream can map cue types (emphasis, think,
listen, etc.) to BeginSpeaking / BeginThinking / TriggerEmphasis helpers.
Steps 1–2 (utterance montage + idle micro) stay on.

Prerequisites: Phase 8 steps 1–2 PASS.

Before running: stop PIE and close the BP_Godfrey_Performer Blueprint editor tab.

Headless:
  UnrealEditor-Cmd.exe "D:/UE Projects/MetaHuman_Baseline_Test/UnrealPerformer.uproject"
    -ExecutePythonScript="D:/UE Projects/MetaHuman_Baseline_Test/Scripts/setup_phase8_step3_performance_cue_routing.py"
    -unattended -nop4 -nosplash -log

PIE PASS GATE: queue TTS unchanged (voice + lip sync + talking montage + IdleMicro=1).
Log: bRoutePerformanceCuesToStates enabled. Brain may still log performance events: none
until the exhibition API sends cues; when cues arrive look for GodfreyPerformer: cue type=...
Zoom shirt 30+ s.
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

REPORT = "Phase8Step3PerformanceCueRouting.txt"
_lines: list[str] = []


def log(msg: str) -> None:
    _lines.append(msg)
    unreal.log(f"[Phase8Step3] {msg}")


def write_report(wiring_ok: bool, save_ok: bool) -> None:
    path = unreal.Paths.convert_relative_path_to_full(
        unreal.Paths.project_saved_dir() + REPORT
    )
    if wiring_ok and save_ok:
        header = "RESULT: PASS\n"
    elif wiring_ok:
        header = "RESULT: PASS_WIRING (save failed — manual Ctrl+S on BP_Godfrey_Performer)\n"
    else:
        header = "RESULT: FAIL\n"
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(header + "\n".join(_lines) + "\n")
    log(f"Report: {path}")


def main() -> None:
    bp = unreal.load_asset(wiring.GODFREY_PERFORMER_BP)
    if not bp:
        raise RuntimeError(f"Missing {wiring.GODFREY_PERFORMER_BP}")

    cue_changes = wiring.configure_performance_cue_routing(bp)
    for key, value in cue_changes.items():
        log(f"PerformanceState.{key} = {value}")

    audit = wiring.audit_step8_performance_cue_routing(bp)
    for key, value in audit.items():
        log(f"performer.audit.{key} = {value}")

    save_ok = wiring.save_godfrey_performer_blueprint(bp)
    if save_ok:
        log("BP_Godfrey_Performer saved to disk.")
    else:
        log(f"WARN: {wiring.BP_SAVE_LOCK_HINT}")
        log(f"NOTE: {wiring.BP_SAVE_HEADLESS_HINT}")

    write_report(wiring_ok=True, save_ok=save_ok)
    log(
        "Phase 8 step 3 wiring complete — PIE: queue TTS; cues route when Brain sends "
        "performance events (may log 'none' until exhibition API provides them)"
    )


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        unreal.log_error(f"[Phase8Step3] {exc}")
        write_report(wiring_ok=False, save_ok=False)
        sys.exit(1)
