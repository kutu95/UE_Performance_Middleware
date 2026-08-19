"""Report unsided eye controls (eyeParallelLookDirection, eyeCornerNarrow, …) on the EyeFixed library.

eyeParallelLookDirection decides whether both eyeballs share one look direction; at 0 each eye
follows its own eyeLook* curves, so any L/R difference reads as a divergent eye.
"""
from __future__ import annotations

import unreal

LIBRARY = "/Game/Godfrey/Animation/Animation/Performances"
SUFFIX = "_EyeFixed"

lib = unreal.AnimationLibrary
FLOAT = unreal.RawCurveTrackTypes.RCT_FLOAT


def stats(seq, name: str):
    ue_name = unreal.Name(name)
    if not lib.does_curve_exist(seq, ue_name, FLOAT):
        return None
    _, values = lib.get_float_keys(seq, ue_name)
    vals = [float(v) for v in values]
    if not vals:
        return (0, 0.0, 0.0, 0.0)
    return (len(vals), sum(vals) / len(vals), min(vals), max(vals))


def main() -> None:
    paths = unreal.EditorAssetLibrary.list_assets(LIBRARY, recursive=False, include_folder=False)
    names = sorted(
        p.rsplit("/", 1)[-1].split(".")[0] for p in paths
        if p.rsplit("/", 1)[-1].split(".")[0].endswith(SUFFIX)
    )

    missing = 0
    zeroed = 0
    for asset_name in names:
        seq = unreal.load_asset(f"{LIBRARY}/{asset_name}")
        if not isinstance(seq, unreal.AnimSequence):
            continue
        all_names = [str(n) for n in lib.get_animation_curve_names(seq, FLOAT)]
        # get_animation_curve_names can return comparison-cased names, so match sides case-insensitively.
        unsided = sorted(
            n for n in all_names
            if "eye" in n.lower() and n.lower()[-1] not in ("l", "r")
        )
        parallel = stats(seq, "CTRL_expressions_eyeParallelLookDirection")
        if parallel is None:
            missing += 1
            desc = "ABSENT"
        else:
            keys, mean, lo, hi = parallel
            if keys == 0 or hi <= 0.001:
                zeroed += 1
            desc = f"keys={keys} mean={mean:.3f} min={lo:.3f} max={hi:.3f}"
        unreal.log(f"[EyeUnsided] {asset_name}: parallelLookDirection {desc} | other unsided eye curves: {unsided}")

    unreal.log(f"[EyeUnsided] === {len(names)} assets: parallelLookDirection absent={missing} present-but-zero={zeroed} ===")


if __name__ == "__main__":
    main()
