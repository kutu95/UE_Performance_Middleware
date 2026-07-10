"""Phase 8 step 1 — queue speech auto-drives speaking idle montage.

Enables bAutoSpeakingStateFromUtterance on GodfreyPerformanceStateComponent so
NotifyUtteranceStarted/Ended from the PCM stream call BeginSpeaking/EndSpeaking,
which triggers GodfreyPerformerAnimationBridge speaking idle montage playback.

Prerequisites: Phase 8 step 0 (SpeakingIdleMontage assigned).

Headless:
  UnrealEditor-Cmd.exe "D:/UE Projects/MetaHuman_Baseline_Test/UnrealPerformer.uproject"
    -ExecutePythonScript="D:/UE Projects/MetaHuman_Baseline_Test/Scripts/setup_phase8_step1_utterance_speaking_state.py"
    -unattended -nop4 -nosplash -log

PIE PASS GATE: exhibition queue TTS -> audible + lip sync + upper-body talking loop.
Log: GodfreyPerformer: auto utterance -> BeginSpeaking
     GodfreyPerformerBridge: playing montage 'As_Godfrey_Talking_Anim_Montage'
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

REPORT = "Phase8Step1UtteranceSpeakingState.txt"
_lines: list[str] = []


def log(msg: str) -> None:
    _lines.append(msg)
    unreal.log(f"[Phase8Step1] {msg}")


def write_report(ok: bool) -> None:
    path = unreal.Paths.convert_relative_path_to_full(
        unreal.Paths.project_saved_dir() + REPORT
    )
    header = "RESULT: PASS\n" if ok else "RESULT: FAIL\n"
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(header + "\n".join(_lines) + "\n")
    log(f"Report: {path}")


def main() -> None:
    wiring.verify_kristofer_speaking_montage_skeleton()

    bp = unreal.load_asset(wiring.GODFREY_PERFORMER_BP)
    if not bp:
        raise RuntimeError(f"Missing {wiring.GODFREY_PERFORMER_BP}")

    montage_changes = wiring.configure_speaking_idle_montage(bp)
    for key, value in montage_changes.items():
        log(f"Bridge.{key} = {value}")

    state_changes = wiring.configure_utterance_speaking_state(bp)
    for key, value in state_changes.items():
        log(f"PerformanceState.{key} = {value}")

    body_changes = wiring.configure_body_for_montage_playback(bp)
    for key, value in body_changes.items():
        log(f"Body.{key} = {value}")

    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    audit = wiring.audit_step8_utterance_speaking_state(bp)
    for key, value in audit.items():
        log(f"performer.audit.{key} = {value}")

    if not wiring.save_godfrey_performer_blueprint(bp):
        raise RuntimeError(wiring.BP_SAVE_LOCK_HINT)

    write_report(True)
    log(
        "Phase 8 step 1 complete — PIE: queue TTS should drive talking montage; "
        "look for auto utterance -> BeginSpeaking and montage play logs"
    )


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        unreal.log_error(f"[Phase8Step1] {exc}")
        write_report(False)
        sys.exit(1)
