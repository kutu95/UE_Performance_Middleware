"""Phase 9 — set up deterministic eye-correction pipeline for solved performances.

Creates a correction plan JSON with source/target assets and statuses, plus a report.
No animation data is modified by this script; it prepares repeatable work items.

Run (editor with project open):
  Tools -> Execute Python Script -> Scripts/setup_phase9_eye_correction_pipeline.py

Headless:
  UnrealEditor-Cmd.exe "D:/UE Projects/MetaHuman_Baseline_UE58_Test/UnrealPerformer.uproject"
    -ExecutePythonScript="D:/UE Projects/MetaHuman_Baseline_UE58_Test/Scripts/setup_phase9_eye_correction_pipeline.py"
    -unattended -nop4 -nosplash -log
"""
from __future__ import annotations

import json
import os
from dataclasses import dataclass, asdict
from datetime import datetime, timezone
from pathlib import Path

import unreal

REPORT = "Phase9EyeCorrectionPipeline.txt"
CATALOG_PATH = Path("Config/GodfreyPerformanceActionCatalog.json")
PLAN_PATH = Path("Config/GodfreyEyeCorrectionPlan.json")

# Conversation-heavy stems where downcast eye bias is most visible.
TARGET_ACTIONS = [
    "Listening_01",
    "Listening_02",
    "ListeningAgreeing_01",
    "ListeningAttentive_01",
    "ListeningConcerned_01",
    "ListeningCurious_01",
    "ListeningNodding_01",
    "ListeningSympathetic_01",
    "Thinking_01",
    "Thinking_02",
    "ThinkingCoy_01",
    "ThinkingDeepBreath_01",
    "ThinkingDeepBreath_02",
    "ThinkingHandToChin_01",
    "ThinkingLookingAway_01",
    "ThinkingLookingToSea_01",
    "ThinkingRemembering_01",
    "ThinkingReturnGaze_01",
    "SpeakingCalmExplanation_01",
    "SpeakingDescribeDistance_01",
    "SpeakingDescribeSequence_01",
    "SpeakingDescribeSize_01",
    "SpeakingExplainDanger_01",
    "SpeakingGentleEmphasis_01",
    "Concerned_01",
    "Concerned_02",
]

_lines: list[str] = []


def log(msg: str) -> None:
    _lines.append(msg)
    unreal.log(f"[Phase9EyeCorrection] {msg}")


def write_report(ok: bool) -> None:
    report_path = unreal.Paths.convert_relative_path_to_full(
        unreal.Paths.project_saved_dir() + REPORT
    )
    header = "RESULT: PASS\n" if ok else "RESULT: FAIL\n"
    with open(report_path, "w", encoding="utf-8") as handle:
        handle.write(header + "\n".join(_lines) + "\n")
    log(f"Report: {report_path}")


@dataclass
class EyeCorrectionItem:
    cue_id: str
    source_as: str
    source_perf: str
    target_as_eye_fixed: str
    target_am_eye_fixed: str
    status: str
    notes: str


def project_root() -> Path:
    return Path(unreal.Paths.project_dir()).resolve()


def asset_exists(path: str) -> bool:
    return unreal.EditorAssetLibrary.does_asset_exist(path)


def load_catalog() -> dict:
    path = project_root() / CATALOG_PATH
    if not path.is_file():
        raise RuntimeError(f"Missing catalog: {path}")
    with open(path, "r", encoding="utf-8") as handle:
        return json.load(handle)


def make_item(library_path: str, cue_id: str) -> EyeCorrectionItem:
    source_as = f"{library_path}/AS_{cue_id}"
    source_perf = f"{library_path}/Perf_{cue_id}"
    target_as = f"{library_path}/AS_{cue_id}_EyeFixed"
    target_am = f"{library_path}/AM_{cue_id}_EyeFixed"

    has_as = asset_exists(source_as)
    has_perf = asset_exists(source_perf)
    status = "pending"
    notes_parts = []
    if not has_as:
        status = "missing_source_as"
        notes_parts.append("AS source asset missing")
    if not has_perf:
        notes_parts.append("Perf source asset missing (optional)")
    if has_as and asset_exists(target_as):
        status = "already_exists"
        notes_parts.append("EyeFixed target already exists")
    if not notes_parts:
        notes_parts.append("Ready for manual eye correction pass")

    return EyeCorrectionItem(
        cue_id=cue_id,
        source_as=source_as,
        source_perf=source_perf,
        target_as_eye_fixed=target_as,
        target_am_eye_fixed=target_am,
        status=status,
        notes="; ".join(notes_parts),
    )


def build_plan(catalog: dict) -> dict:
    library_path = catalog.get("libraryPath", "/Game/Godfrey/Animation/Animation/Performances")
    catalog_actions = set(catalog.get("actions", []))

    items: list[EyeCorrectionItem] = []
    for cue_id in TARGET_ACTIONS:
        if cue_id not in catalog_actions:
            # Keep entries so missing/renamed cues are explicit.
            item = make_item(library_path, cue_id)
            item.status = "not_in_catalog"
            item.notes = "Cue not present in action catalog"
            items.append(item)
            continue
        items.append(make_item(library_path, cue_id))

    created_utc = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    return {
        "version": 1,
        "createdUtc": created_utc,
        "libraryPath": library_path,
        "namingConvention": {
            "sourceAS": "AS_<CueId>",
            "sourcePerf": "Perf_<CueId>",
            "targetAS": "AS_<CueId>_EyeFixed",
            "targetAM": "AM_<CueId>_EyeFixed",
        },
        "workflow": [
            "Open Perf_<CueId> in MetaHuman Animator.",
            "Apply eye correction using neutral forward gaze reference.",
            "Export/bake corrected animation sequence as AS_<CueId>_EyeFixed.",
            "Create AM_<CueId>_EyeFixed on DefaultSlot if montage variant is used.",
            "Mark item status done and add notes.",
        ],
        "items": [asdict(item) for item in items],
    }


def save_plan(plan: dict) -> Path:
    path = project_root() / PLAN_PATH
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w", encoding="utf-8") as handle:
        json.dump(plan, handle, indent=2)
        handle.write("\n")
    return path


def summarize(plan: dict) -> None:
    items = plan["items"]
    total = len(items)
    ready = sum(1 for i in items if i["status"] == "pending")
    exists = sum(1 for i in items if i["status"] == "already_exists")
    missing = sum(1 for i in items if i["status"] == "missing_source_as")
    out_of_catalog = sum(1 for i in items if i["status"] == "not_in_catalog")

    log(f"Plan items: total={total}, pending={ready}, already_exists={exists}, missing_source_as={missing}, not_in_catalog={out_of_catalog}")
    log("Seed target clip: Listening_01 -> AS_Listening_01_EyeFixed")


def main() -> None:
    catalog = load_catalog()
    plan = build_plan(catalog)
    path = save_plan(plan)
    summarize(plan)
    log(f"Wrote plan: {path}")
    write_report(True)


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        unreal.log_error(f"[Phase9EyeCorrection] {exc}")
        write_report(False)
        raise
