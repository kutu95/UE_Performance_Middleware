"""Phase 8 step 2 — idle micro-motion between utterances.

Enables bEnableIdleMicroMotion on GodfreyPerformerAnimationBridgeComponent so the bridge
behaviour tick updates IdleBreathingWave / IdlePostureSwayWave when not speaking.
Speaking montage + utterance auto-routing from step 1 stay on.

Prerequisites: Phase 8 step 1 PASS (bAutoSpeakingStateFromUtterance + SpeakingIdleMontage).

Before running: stop PIE and close the BP_Godfrey_Performer Blueprint editor tab (avoids
Windows file-lock error 32 on save).

Headless:
  UnrealEditor-Cmd.exe "D:/UE Projects/MetaHuman_Baseline_Test/UnrealPerformer.uproject"
    -ExecutePythonScript="D:/UE Projects/MetaHuman_Baseline_Test/Scripts/setup_phase8_step2_idle_micro_motion.py"
    -unattended -nop4 -nosplash -log

PIE PASS GATE: queue TTS still drives voice + lip sync + talking montage; after EndSpeaking
log shows ReturnedToIdle and behaviour tick IdleMicro=1. Zoom shirt 30+ s.
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

REPORT = "Phase8Step2IdleMicroMotion.txt"
_lines: list[str] = []


def log(msg: str) -> None:
    _lines.append(msg)
    unreal.log(f"[Phase8Step2] {msg}")


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
    wiring.verify_kristofer_speaking_montage_skeleton()

    bp = unreal.load_asset(wiring.GODFREY_PERFORMER_BP)
    if not bp:
        raise RuntimeError(f"Missing {wiring.GODFREY_PERFORMER_BP}")

    # Step 2 delta only — do not re-run step 0/1 bridge reset (that clears idle micro).
    idle_changes = wiring.configure_idle_micro_motion(bp)
    for key, value in idle_changes.items():
        log(f"Bridge.{key} = {value}")

    audit = wiring.audit_step8_idle_micro_motion(bp)
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
        "Phase 8 step 2 wiring complete — PIE: queue TTS + idle between lines; "
        "look for behaviour tick IdleMicro=1 after EndSpeaking"
    )


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        unreal.log_error(f"[Phase8Step2] {exc}")
        write_report(wiring_ok=False, save_ok=False)
        sys.exit(1)
