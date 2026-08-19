"""Summarise Saved/EyeCurveSymmetryAudit.json (written by audit_eye_curve_symmetry.py).

Plain CPython — run outside the editor:
  python Scripts/analyze_eye_symmetry_report.py
"""
from __future__ import annotations

import json
from collections import defaultdict
from pathlib import Path

DATA = Path(__file__).resolve().parents[1] / "Saved" / "EyeCurveSymmetryAudit.json"


def new_row():
    return {
        "clips": 0,
        "sumL": 0.0,
        "sumR": 0.0,
        "sumGap": 0.0,
        "srcSumGap": 0.0,
        "maxGap": 0.0,
        "maxGapClip": "",
        "leftHigher": 0,
        "rightHigher": 0,
        "sumDeltaL": 0.0,
        "sumDeltaR": 0.0,
        "peakL": 0.0,
        "peakR": 0.0,
    }


def main() -> None:
    results = json.loads(DATA.read_text(encoding="utf-8"))
    agg = defaultdict(new_row)

    for clip in results:
        for base, entry in clip["curves"].items():
            fixed_l, fixed_r = entry.get("fixedL"), entry.get("fixedR")
            if not fixed_l or not fixed_r:
                continue
            row = agg[base]
            row["clips"] += 1
            row["sumL"] += fixed_l["mean"]
            row["sumR"] += fixed_r["mean"]
            row["peakL"] = max(row["peakL"], fixed_l["max"])
            row["peakR"] = max(row["peakR"], fixed_r["max"])
            gap = fixed_l["mean"] - fixed_r["mean"]
            row["sumGap"] += abs(gap)
            row["srcSumGap"] += entry.get("sourceGap", 0.0)
            if gap > 0.002:
                row["leftHigher"] += 1
            elif gap < -0.002:
                row["rightHigher"] += 1
            if abs(gap) > abs(row["maxGap"]):
                row["maxGap"] = gap
                row["maxGapClip"] = clip["stem"]
            row["sumDeltaL"] += entry.get("deltaL", 0.0)
            row["sumDeltaR"] += entry.get("deltaR", 0.0)

    print(f"{'curve (L vs R)':<34}{'n':>4}{'meanL':>8}{'meanR':>8}{'avgGap':>8}{'srcGap':>8}{'L>R':>5}{'R>L':>5}{'worst':>8}  worstClip")
    for base, row in sorted(agg.items(), key=lambda kv: -(kv[1]["sumGap"] / max(kv[1]["clips"], 1))):
        n = row["clips"]
        short = base.replace("CTRL_expressions_", "")
        print(
            f"{short:<34}{n:>4}{row['sumL']/n:>8.3f}{row['sumR']/n:>8.3f}{row['sumGap']/n:>8.3f}"
            f"{row['srcSumGap']/n:>8.3f}{row['leftHigher']:>5}{row['rightHigher']:>5}"
            f"{row['maxGap']:>8.3f}  {row['maxGapClip']}"
        )

    print("\n=== correction applied per side (mean delta over clips) ===")
    for base, row in sorted(agg.items()):
        n = row["clips"]
        delta_l, delta_r = row["sumDeltaL"] / n, row["sumDeltaR"] / n
        if abs(delta_l) < 1e-6 and abs(delta_r) < 1e-6:
            continue
        short = base.replace("CTRL_expressions_", "")
        print(f"{short:<34} deltaL={delta_l:+.4f} deltaR={delta_r:+.4f}  diff={delta_l - delta_r:+.4f}")

    print("\n=== clips with the largest total L/R gap after the fix ===")
    per_clip = []
    for clip in results:
        pairs = [
            (abs(e["fixedL"]["mean"] - e["fixedR"]["mean"]), b)
            for b, e in clip["curves"].items()
            if e.get("fixedL") and e.get("fixedR")
        ]
        if not pairs:
            continue
        per_clip.append((sum(p[0] for p in pairs), clip["stem"], max(pairs)))
    for total, stem, worst in sorted(per_clip, reverse=True)[:12]:
        print(f"{stem:<44} totalGap={total:.3f}  worst={worst[1].replace('CTRL_expressions_', '')} {worst[0]:.3f}")


if __name__ == "__main__":
    main()
