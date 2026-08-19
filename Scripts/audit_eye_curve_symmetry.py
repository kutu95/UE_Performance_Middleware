"""Audit left/right symmetry of eye curves on the Godfrey performance library (UE 5.8).

Answers two questions for every AS_<stem> / AS_<stem>_EyeFixed pair:
  1. Was the correction applied equally to both eyes? (delta L vs delta R per curve pair)
  2. Is the corrected result symmetric? (value L vs R, and whether the fix widened the gap)

A curve present on only one side, or a delta that differs between sides, is reported as a finding.

Headless:
  UnrealEditor-Cmd.exe "D:/UE Projects/MetaHuman_Baseline_UE58_Test/UnrealPerformer.uproject"
    -ExecutePythonScript="D:/UE Projects/MetaHuman_Baseline_UE58_Test/Scripts/audit_eye_curve_symmetry.py"
    -unattended -nop4 -nosplash -log
"""
from __future__ import annotations

import json

import unreal

LIBRARY = "/Game/Godfrey/Animation/Animation/Performances"
SUFFIX = "_EyeFixed"

REPORT_TXT = "EyeCurveSymmetryAudit.txt"
REPORT_JSON = "EyeCurveSymmetryAudit.json"

# Curve families that can make one eye read as "wrong": gaze, lids, blink, squint.
EYE_TOKENS = ("eyelook", "eyeblink", "eyewiden", "eyesquint", "eyelid", "eyerelax", "eyecorner", "eyeparallel")

# Deltas below this are treated as equal (float key noise).
EQUAL_TOLERANCE = 0.002

_lines: list[str] = []


def log(msg: str) -> None:
    _lines.append(msg)
    unreal.log(f"[EyeSymmetry] {msg}")


def saved_path(filename: str) -> str:
    return unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir() + filename)


def curve_names(seq) -> list[str]:
    names = unreal.AnimationLibrary.get_animation_curve_names(seq, unreal.RawCurveTrackTypes.RCT_FLOAT)
    return [str(n) for n in names]


def curve_stats(seq, name: str):
    lib = unreal.AnimationLibrary
    ue_name = unreal.Name(name)
    if not lib.does_curve_exist(seq, ue_name, unreal.RawCurveTrackTypes.RCT_FLOAT):
        return None
    _, values = lib.get_float_keys(seq, ue_name)
    vals = [float(v) for v in values]
    if not vals:
        return {"keys": 0, "mean": 0.0, "min": 0.0, "max": 0.0}
    return {
        "keys": len(vals),
        "mean": sum(vals) / len(vals),
        "min": min(vals),
        "max": max(vals),
    }


def is_eye_curve(name: str) -> bool:
    lowered = name.lower()
    return any(token in lowered for token in EYE_TOKENS)


def side_of(name: str):
    """('base', 'L'|'R') for sided curves, else None."""
    if name.endswith("L"):
        return name[:-1], "L"
    if name.endswith("R"):
        return name[:-1], "R"
    return None


def eye_curve_pairs(names: list[str]):
    """{base: {'L': name, 'R': name}} for sided eye curves."""
    pairs: dict[str, dict[str, str]] = {}
    for name in names:
        if not is_eye_curve(name):
            continue
        sided = side_of(name)
        if not sided:
            continue
        base, side = sided
        pairs.setdefault(base, {})[side] = name
    return pairs


def audit_stem(stem: str):
    source_path = f"{LIBRARY}/AS_{stem}"
    fixed_path = f"{LIBRARY}/AS_{stem}{SUFFIX}"

    source = unreal.load_asset(source_path)
    fixed = unreal.load_asset(fixed_path)
    if not isinstance(source, unreal.AnimSequence) or not isinstance(fixed, unreal.AnimSequence):
        log(f"{stem}: SKIP (missing source or EyeFixed AnimSequence)")
        return None

    source_names = curve_names(source)
    fixed_names = curve_names(fixed)
    pairs = eye_curve_pairs(sorted(set(source_names) | set(fixed_names)))

    result = {
        "stem": stem,
        "unpairedCurves": [],
        "unequalDeltas": [],
        "asymmetricResult": [],
        "curves": {},
    }

    for base, sides in sorted(pairs.items()):
        left_name = sides.get("L")
        right_name = sides.get("R")
        if not left_name or not right_name:
            result["unpairedCurves"].append(left_name or right_name)
            continue

        src_l = curve_stats(source, left_name)
        src_r = curve_stats(source, right_name)
        fix_l = curve_stats(fixed, left_name)
        fix_r = curve_stats(fixed, right_name)

        entry = {
            "sourceL": src_l,
            "sourceR": src_r,
            "fixedL": fix_l,
            "fixedR": fix_r,
        }
        result["curves"][base] = entry

        # Curve exists on one side only after the fix — the correction cannot be symmetric.
        if (fix_l is None) != (fix_r is None):
            result["unpairedCurves"].append(left_name if fix_l is None else right_name)
            continue
        if fix_l is None or fix_r is None:
            continue

        src_l_mean = src_l["mean"] if src_l else 0.0
        src_r_mean = src_r["mean"] if src_r else 0.0
        delta_l = fix_l["mean"] - src_l_mean
        delta_r = fix_r["mean"] - src_r_mean
        entry["deltaL"] = delta_l
        entry["deltaR"] = delta_r

        if abs(delta_l - delta_r) > EQUAL_TOLERANCE:
            result["unequalDeltas"].append(
                {"curve": base, "deltaL": delta_l, "deltaR": delta_r, "diff": delta_l - delta_r}
            )

        source_gap = abs(src_l_mean - src_r_mean)
        fixed_gap = abs(fix_l["mean"] - fix_r["mean"])
        entry["sourceGap"] = source_gap
        entry["fixedGap"] = fixed_gap
        if fixed_gap > EQUAL_TOLERANCE:
            result["asymmetricResult"].append(
                {
                    "curve": base,
                    "fixedL": fix_l["mean"],
                    "fixedR": fix_r["mean"],
                    "fixedGap": fixed_gap,
                    "sourceGap": source_gap,
                    "widened": fixed_gap > source_gap + EQUAL_TOLERANCE,
                }
            )

    return result


def stems_with_eye_fixed() -> list[str]:
    asset_paths = unreal.EditorAssetLibrary.list_assets(LIBRARY, recursive=False, include_folder=False)
    stems = []
    for path in asset_paths:
        name = path.rsplit("/", 1)[-1].split(".")[0]
        if not name.startswith("AS_") or not name.endswith(SUFFIX):
            continue
        stems.append(name[3 : -len(SUFFIX)])
    stems.sort()
    return stems


def main() -> None:
    stems = stems_with_eye_fixed()
    log(f"Auditing {len(stems)} EyeFixed sequences under {LIBRARY}")

    results = []
    for stem in stems:
        try:
            result = audit_stem(stem)
        except Exception as exc:
            log(f"{stem}: ERROR {exc}")
            continue
        if not result:
            continue
        results.append(result)

        flags = []
        if result["unpairedCurves"]:
            flags.append(f"unpaired={result['unpairedCurves']}")
        if result["unequalDeltas"]:
            flags.append(
                "unequalDelta="
                + ", ".join(
                    f"{d['curve']} L{d['deltaL']:+.3f} R{d['deltaR']:+.3f} (diff {d['diff']:+.3f})"
                    for d in result["unequalDeltas"]
                )
            )
        widened = [a for a in result["asymmetricResult"] if a["widened"]]
        if widened:
            flags.append(
                "gapWidened="
                + ", ".join(
                    f"{a['curve']} L{a['fixedL']:.3f} vs R{a['fixedR']:.3f} (src gap {a['sourceGap']:.3f} -> {a['fixedGap']:.3f})"
                    for a in widened
                )
            )
        log(f"{stem}: " + ("; ".join(flags) if flags else "symmetric"))

    total_unpaired = sum(len(r["unpairedCurves"]) for r in results)
    total_unequal = sum(len(r["unequalDeltas"]) for r in results)
    total_asym = sum(len(r["asymmetricResult"]) for r in results)
    total_widened = sum(len([a for a in r["asymmetricResult"] if a["widened"]]) for r in results)

    log("")
    log("=== SUMMARY ===")
    log(f"sequences audited      : {len(results)}")
    log(f"curves on one side only: {total_unpaired}")
    log(f"unequal L/R deltas     : {total_unequal}")
    log(f"asymmetric results     : {total_asym} (of which widened by the fix: {total_widened})")

    with open(saved_path(REPORT_TXT), "w", encoding="utf-8") as handle:
        handle.write("\n".join(_lines) + "\n")
    with open(saved_path(REPORT_JSON), "w", encoding="utf-8") as handle:
        json.dump(results, handle, indent=1)
    unreal.log(f"[EyeSymmetry] reports: {saved_path(REPORT_TXT)} / {saved_path(REPORT_JSON)}")


if __name__ == "__main__":
    main()
