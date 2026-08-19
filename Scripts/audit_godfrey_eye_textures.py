"""Audit Godfrey's baked eye textures for a left/right brightness difference (UE 5.8).

Dumps the import/adjustment settings of every baked eye texture, then exports the
iris and sclera basecolor maps to PNG and measures mean luminance per side so the
"one eye is lighter" report can be confirmed numerically.

Read-only with respect to assets. Writes Saved/GodfreyEyeTextureAudit.txt and
PNG exports under Saved/EyeTexExport/.

Headless (copy to a space-free path first, UE splits the arg on "UE Projects"):
  UnrealEditor-Cmd.exe "D:/UE Projects/MetaHuman_Baseline_UE58_Test/UnrealPerformer.uproject"
    -ExecutePythonScript="D:/godfrey_eye_tex_tmp.py"
    -unattended -nop4 -nosplash -NullRHI -log
"""
from __future__ import annotations

import os

import unreal

BAKED = "/Game/MetaHumans/MHC_CaptainGodfrey/Face/Baked/"

PAIRS = [
    ("T_EyeIrisL_BC", "T_EyeIrisR_BC"),
    ("T_EyeIrisL_N", "T_EyeIrisR_N"),
    ("T_EyeScleraL_BC", "T_EyeScleraR_BC"),
    ("T_EyeScleraL_N", "T_EyeScleraR_N"),
]

EXPORT_PAIRS = [("T_EyeIrisL_BC", "T_EyeIrisR_BC"), ("T_EyeScleraL_BC", "T_EyeScleraR_BC")]

# Texture asset settings that can shift apparent brightness on their own.
SETTINGS = [
    "srgb",
    "compression_settings",
    "compression_no_alpha",
    "lod_group",
    "lod_bias",
    "mip_gen_settings",
    "adjust_brightness",
    "adjust_brightness_curve",
    "adjust_saturation",
    "adjust_vibrance",
    "adjust_hue",
    "adjust_rgb_curve",
    "adjust_min_alpha",
    "adjust_max_alpha",
    "virtual_texture_streaming",
]

REPORT_TXT = "GodfreyEyeTextureAudit.txt"
EXPORT_DIR = "EyeTexExport"

_lines: list[str] = []


def log(msg: str) -> None:
    _lines.append(msg)
    unreal.log(f"[EyeTex] {msg}")


def saved_path(*parts: str) -> str:
    return unreal.Paths.convert_relative_path_to_full(os.path.join(unreal.Paths.project_saved_dir(), *parts))


def prop(obj, name, default="<unavailable>"):
    try:
        return obj.get_editor_property(name)
    except Exception:
        return default


def describe(tex) -> dict:
    info = {
        "size": f"{tex.blueprint_get_size_x()}x{tex.blueprint_get_size_y()}",
        "format": str(prop(tex, "compression_settings")),
    }
    for name in SETTINGS:
        info[name] = str(prop(tex, name))
    return info


def export_png(tex, out_path: str) -> bool:
    task = unreal.AssetExportTask()
    task.set_editor_property("object", tex)
    task.set_editor_property("filename", out_path)
    task.set_editor_property("automated", True)
    task.set_editor_property("prompt", False)
    task.set_editor_property("replace_identical", True)
    try:
        ok = unreal.Exporter.run_asset_export_task(task)
    except Exception as exc:
        log(f"  export failed for {tex.get_name()}: {exc}")
        return False
    return bool(ok) and os.path.exists(out_path)


def png_stats(path: str, sample_step: int = 4) -> dict | None:
    """Mean linear-ish luminance of a PNG, decoded with the stdlib only."""
    import struct
    import zlib

    with open(path, "rb") as handle:
        data = handle.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        return None

    pos = 8
    width = height = depth = color_type = 0
    idat = bytearray()
    while pos < len(data):
        length = struct.unpack(">I", data[pos:pos + 4])[0]
        kind = data[pos + 4:pos + 8]
        chunk = data[pos + 8:pos + 8 + length]
        if kind == b"IHDR":
            width, height, depth, color_type = struct.unpack(">IIBB", chunk[:10])
        elif kind == b"IDAT":
            idat += chunk
        elif kind == b"IEND":
            break
        pos += 12 + length

    if depth != 8 or color_type not in (2, 6):
        return {"note": f"unsupported png depth={depth} color_type={color_type}", "width": width, "height": height}

    channels = 3 if color_type == 2 else 4
    raw = zlib.decompress(bytes(idat))
    stride = width * channels

    total = [0.0, 0.0, 0.0]
    opaque_total = [0.0, 0.0, 0.0]
    count = 0
    opaque_count = 0
    prev = bytearray(stride)
    offset = 0
    for y in range(height):
        filter_type = raw[offset]
        offset += 1
        line = bytearray(raw[offset:offset + stride])
        offset += stride

        if filter_type == 1:
            for i in range(channels, stride):
                line[i] = (line[i] + line[i - channels]) & 0xFF
        elif filter_type == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 0xFF
        elif filter_type == 3:
            for i in range(stride):
                left = line[i - channels] if i >= channels else 0
                line[i] = (line[i] + ((left + prev[i]) >> 1)) & 0xFF
        elif filter_type == 4:
            for i in range(stride):
                left = line[i - channels] if i >= channels else 0
                up = prev[i]
                upleft = prev[i - channels] if i >= channels else 0
                p = left + up - upleft
                pa, pb, pc = abs(p - left), abs(p - up), abs(p - upleft)
                pred = left if (pa <= pb and pa <= pc) else (up if pb <= pc else upleft)
                line[i] = (line[i] + pred) & 0xFF

        if y % sample_step == 0:
            for x in range(0, width, sample_step):
                i = x * channels
                r, g, b = line[i], line[i + 1], line[i + 2]
                total[0] += r
                total[1] += g
                total[2] += b
                count += 1
                if channels == 3 or line[i + 3] > 200:
                    opaque_total[0] += r
                    opaque_total[1] += g
                    opaque_total[2] += b
                    opaque_count += 1
        prev = line

    def mean(acc, n):
        return [v / max(n, 1) for v in acc]

    rgb = mean(total, count)
    orgb = mean(opaque_total, opaque_count)
    return {
        "width": width,
        "height": height,
        "channels": channels,
        "mean_rgb": rgb,
        "mean_luma": 0.2126 * rgb[0] + 0.7152 * rgb[1] + 0.0722 * rgb[2],
        "opaque_mean_rgb": orgb,
        "opaque_mean_luma": 0.2126 * orgb[0] + 0.7152 * orgb[1] + 0.0722 * orgb[2],
        "opaque_samples": opaque_count,
        "samples": count,
    }


def main() -> None:
    log("=== Baked eye texture settings ===")
    for left_name, right_name in PAIRS:
        left = unreal.EditorAssetLibrary.load_asset(BAKED + left_name)
        right = unreal.EditorAssetLibrary.load_asset(BAKED + right_name)
        if left is None or right is None:
            log(f"{left_name} / {right_name}: MISSING")
            continue
        linfo, rinfo = describe(left), describe(right)
        log(f"\n{left_name}  vs  {right_name}")
        for key in linfo:
            flag = "" if linfo[key] == rinfo[key] else "   <== MISMATCH"
            log(f"  {key:<28} {linfo[key]:<24} {rinfo[key]:<24}{flag}")
    log("")

    out_dir = saved_path(EXPORT_DIR)
    os.makedirs(out_dir, exist_ok=True)

    log("=== Pixel statistics (mean of exported PNG, 0-255) ===")
    for left_name, right_name in EXPORT_PAIRS:
        stats = {}
        for name in (left_name, right_name):
            tex = unreal.EditorAssetLibrary.load_asset(BAKED + name)
            if tex is None:
                continue
            path = os.path.join(out_dir, name + ".png")
            if not export_png(tex, path):
                log(f"  {name}: PNG export failed")
                continue
            stats[name] = png_stats(path)

        log(f"\n{left_name}  vs  {right_name}")
        for name, value in stats.items():
            if value is None:
                log(f"  {name}: could not decode")
            elif "note" in value:
                log(f"  {name}: {value}")
            else:
                rgb = value["mean_rgb"]
                org = value["opaque_mean_rgb"]
                log(f"  {name:<20} {value['width']}x{value['height']} ch={value['channels']}")
                log(f"      mean rgb        ({rgb[0]:7.3f}, {rgb[1]:7.3f}, {rgb[2]:7.3f})  luma {value['mean_luma']:7.3f}")
                log(f"      opaque mean rgb ({org[0]:7.3f}, {org[1]:7.3f}, {org[2]:7.3f})  luma {value['opaque_mean_luma']:7.3f}"
                    f"  ({value['opaque_samples']} samples)")

        if len(stats) == 2 and all(v and "note" not in v for v in stats.values()):
            lv, rv = stats[left_name], stats[right_name]
            delta = lv["mean_luma"] - rv["mean_luma"]
            pct = 100.0 * delta / max(rv["mean_luma"], 1e-6)
            log(f"  DELTA luma (L - R) = {delta:+.3f}  ({pct:+.2f}% vs right)")
            if abs(pct) >= 1.0:
                log("  FINDING: left and right baked maps differ in overall brightness.")

    with open(saved_path(REPORT_TXT), "w", encoding="utf-8") as handle:
        handle.write("\n".join(_lines) + "\n")
    unreal.log(f"[EyeTex] wrote {saved_path(REPORT_TXT)}")


main()
