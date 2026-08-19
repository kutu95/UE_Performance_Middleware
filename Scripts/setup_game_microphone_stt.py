"""Add always-on game microphone STT beside the existing web exhibition queue.

Preserves:
  - Browser Web Speech + POST /api/chat + unreal TTS queue
  - GodfreyExhibitionQueuePollComponent on GM_Godfrey_Exhibit

Adds on GM_Godfrey_Exhibit:
  - GodfreyDirectSpeech (text → stream-pcm; no G-key)
  - GodfreyVoiceInput (mic → Brain OpenAI realtime STT → AskGodfrey)

Prerequisites:
  - Rebuild UnrealPerformer / UnrealPerformerEditor after pulling C++
  - Godfrey Brain running with OPENAI_API_KEY (ws://localhost:3000/api/unreal/stt)
  - Windows mic permission granted to Unreal Editor / packaged game

Headless:
  UnrealEditor-Cmd.exe ".../UnrealPerformer.uproject"
    /Game/Godfrey_World
    -ExecutePythonScript=".../Scripts/setup_game_microphone_stt.py"
    -unattended -nop4 -nosplash -log
"""
from __future__ import annotations

import os
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

import unreal

import godfrey_blueprint_wiring as wiring  # noqa: E402

REPORT = "GameMicrophoneStt.txt"
_lines: list[str] = []


def log(msg: str) -> None:
    _lines.append(msg)
    unreal.log(f"[GameMicSTT] {msg}")


def write_report(ok: bool) -> None:
    path = unreal.Paths.convert_relative_path_to_full(
        unreal.Paths.project_saved_dir() + REPORT
    )
    header = "RESULT: PASS\n" if ok else "RESULT: FAIL\n"
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(header + "\n".join(_lines) + "\n")
    log(f"Report: {path}")


def verify_cpp_classes() -> None:
    for class_path in (
        wiring.QUEUE_POLL_CLASS,
        wiring.DIRECT_SPEECH_CLASS,
        wiring.VOICE_INPUT_CLASS,
    ):
        if not unreal.load_class(None, class_path):
            raise RuntimeError(
                f"Class not loaded: {class_path}. "
                "Rebuild UnrealPerformerEditor and restart the editor."
            )
        log(f"C++ class OK: {class_path}")


def configure_gamemode(gm_bp) -> None:
    # Keep web path.
    if not wiring.find_component_by_class(gm_bp, "GodfreyExhibitionQueuePollComponent")[0]:
        added_poll = wiring.add_exhibition_queue_poll(gm_bp)
        log("Added GodfreyExhibitionQueuePoll" if added_poll else "Queue poll add failed")
        wiring.configure_exhibition_queue_poll(gm_bp)
    else:
        poll_changes = wiring.configure_exhibition_queue_poll(gm_bp)
        for key, value in poll_changes.items():
            log(f"QueuePoll.{key} = {value}")

    added_speech = wiring.add_direct_speech(gm_bp)
    log("Added GodfreyDirectSpeech" if added_speech else "GodfreyDirectSpeech already present")
    for key, value in wiring.configure_direct_speech_for_game_mic(gm_bp).items():
        log(f"DirectSpeech.{key} = {value}")

    added_voice = wiring.add_voice_input(gm_bp)
    log("Added GodfreyVoiceInput" if added_voice else "GodfreyVoiceInput already present")
    for key, value in wiring.configure_voice_input(gm_bp).items():
        log(f"VoiceInput.{key} = {value}")

    audit = wiring.audit_gamemode_game_mic(gm_bp)
    for key, value in audit.items():
        log(f"gm.audit.{key} = {value}")

    unreal.BlueprintEditorLibrary.compile_blueprint(gm_bp)
    unreal.EditorAssetLibrary.save_loaded_assets([gm_bp])


def main() -> None:
    verify_cpp_classes()

    gm_bp = unreal.EditorAssetLibrary.load_asset(wiring.GM_GODFREY_EXHIBIT)
    if not gm_bp:
        raise RuntimeError(f"Failed to load {wiring.GM_GODFREY_EXHIBIT}")

    configure_gamemode(gm_bp)
    write_report(True)


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        log(f"ERROR: {exc}")
        write_report(False)
        raise
