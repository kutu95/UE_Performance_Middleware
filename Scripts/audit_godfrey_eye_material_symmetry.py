"""Audit left/right symmetry of Godfrey's baked eye material instances (UE 5.8).

Reports, for MI_EyeL_Baked vs MI_EyeR_Baked:
  1. Parent chain (they are expected to differ - this is itself a finding).
  2. Every scalar / vector / texture / static-switch parameter, override value and
     effective value, side by side, flagging any mismatch.
  3. Which material slot on the face mesh uses which instance.

Read-only. Writes Saved/GodfreyEyeMaterialSymmetry.txt and .json.

Headless (copy to a space-free path first, UE splits the arg on "UE Projects"):
  UnrealEditor-Cmd.exe "D:/UE Projects/MetaHuman_Baseline_UE58_Test/UnrealPerformer.uproject"
    -ExecutePythonScript="D:/godfrey_eye_audit_tmp.py"
    -unattended -nop4 -nosplash -NullRHI -log
"""
from __future__ import annotations

import json

import unreal

EYE_L = "/Game/MetaHumans/MHC_CaptainGodfrey/Face/Materials/MI_EyeL_Baked"
EYE_R = "/Game/MetaHumans/MHC_CaptainGodfrey/Face/Materials/MI_EyeR_Baked"
FACE_MESH = "/Game/MetaHumans/MHC_CaptainGodfrey/Face/SKM_MHC_CaptainGodfrey_FaceMesh"

REPORT_TXT = "GodfreyEyeMaterialSymmetry.txt"
REPORT_JSON = "GodfreyEyeMaterialSymmetry.json"

# Float compare tolerance for "same value".
EQUAL_TOLERANCE = 1e-4

_lines: list[str] = []


def log(msg: str) -> None:
    _lines.append(msg)
    unreal.log(f"[EyeMaterial] {msg}")


def saved_path(filename: str) -> str:
    return unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir() + filename)


def load(path: str):
    asset = unreal.EditorAssetLibrary.load_asset(path)
    if asset is None:
        log(f"MISSING ASSET: {path}")
    return asset


def prop(obj, name, default=None):
    try:
        return obj.get_editor_property(name)
    except Exception:
        return default


def parent_chain(mi) -> list[str]:
    chain = []
    node = mi
    seen = set()
    while node is not None:
        pkg = node.get_path_name()
        if pkg in seen:
            break
        seen.add(pkg)
        chain.append(pkg)
        node = prop(node, "parent")
    return chain


def overrides(mi) -> dict:
    """Explicit overrides stored on this instance (not inherited)."""
    out = {"scalar": {}, "vector": {}, "texture": {}, "switch": {}}

    for entry in prop(mi, "scalar_parameter_values", []) or []:
        info = prop(entry, "parameter_info")
        out["scalar"][str(prop(info, "name"))] = float(prop(entry, "parameter_value", 0.0))

    for entry in prop(mi, "vector_parameter_values", []) or []:
        info = prop(entry, "parameter_info")
        col = prop(entry, "parameter_value")
        out["vector"][str(prop(info, "name"))] = [
            float(prop(col, "r", 0.0)),
            float(prop(col, "g", 0.0)),
            float(prop(col, "b", 0.0)),
            float(prop(col, "a", 0.0)),
        ]

    for entry in prop(mi, "texture_parameter_values", []) or []:
        info = prop(entry, "parameter_info")
        tex = prop(entry, "parameter_value")
        out["texture"][str(prop(info, "name"))] = tex.get_path_name() if tex else "None"

    static = prop(mi, "static_parameters")
    switches = prop(static, "static_switch_parameters", []) if static is not None else []
    for entry in switches or []:
        info = prop(entry, "parameter_info")
        out["switch"][str(prop(info, "name"))] = bool(prop(entry, "value", False))

    return out


def all_parameter_names(mi) -> dict:
    """Every parameter exposed by the root material, by type."""
    mel = unreal.MaterialEditingLibrary
    base = mi.get_base_material() if hasattr(mi, "get_base_material") else prop(mi, "parent")
    names = {"scalar": [], "vector": [], "texture": [], "switch": []}
    getters = {
        "scalar": "get_scalar_parameter_names",
        "vector": "get_vector_parameter_names",
        "texture": "get_texture_parameter_names",
        "switch": "get_static_switch_parameter_names",
    }
    for kind, fn_name in getters.items():
        fn = getattr(mel, fn_name, None)
        if fn is None or base is None:
            continue
        try:
            names[kind] = [str(n) for n in fn(base)]
        except Exception as exc:
            log(f"  ! {fn_name} failed: {exc}")
    return names


def effective(mi, kind: str, name: str):
    """Value the renderer actually uses, resolving inheritance."""
    mel = unreal.MaterialEditingLibrary
    fn_name = {
        "scalar": "get_material_instance_scalar_parameter_value",
        "vector": "get_material_instance_vector_parameter_value",
        "texture": "get_material_instance_texture_parameter_value",
        "switch": "get_material_instance_static_switch_parameter_value",
    }[kind]
    fn = getattr(mel, fn_name, None)
    if fn is None:
        return None
    try:
        value = fn(mi, unreal.Name(name))
    except Exception:
        return None
    if kind == "vector" and value is not None:
        return [float(value.r), float(value.g), float(value.b), float(value.a)]
    if kind == "texture":
        return value.get_path_name() if value else "None"
    if kind == "scalar" and value is not None:
        return float(value)
    return value


def same(kind: str, a, b) -> bool:
    if a is None or b is None:
        return a is b
    if kind == "scalar":
        return abs(a - b) <= EQUAL_TOLERANCE
    if kind == "vector":
        return all(abs(x - y) <= EQUAL_TOLERANCE for x, y in zip(a, b))
    if kind == "texture":
        # Baked textures are legitimately per-side (…IrisL_BC vs …IrisR_BC).
        return a.replace("L_", "R_").replace("_L", "_R") == b.replace("L_", "R_").replace("_L", "_R")
    return a == b


def fmt(value) -> str:
    if isinstance(value, list):
        return "(" + ", ".join(f"{v:.5f}" for v in value) + ")"
    if isinstance(value, float):
        return f"{value:.5f}"
    return str(value)


def main() -> None:
    left = load(EYE_L)
    right = load(EYE_R)
    if left is None or right is None:
        log("RESULT: FAIL - could not load both eye material instances")
        return

    report = {"left": EYE_L, "right": EYE_R, "parents": {}, "parameters": {}, "mesh_slots": {}}

    log("=== Parent chains ===")
    lchain = parent_chain(left)
    rchain = parent_chain(right)
    report["parents"] = {"left": lchain, "right": rchain}
    for label, chain in (("L", lchain), ("R", rchain)):
        log(f"{label}:")
        for depth, node in enumerate(chain):
            log(f"   {'  ' * depth}{node}")
    if lchain[1:] != rchain[1:]:
        log("FINDING: left and right inherit from DIFFERENT parents.")
    log("")

    lover = overrides(left)
    rover = overrides(right)
    names = all_parameter_names(left)
    # Union in case the two sides sit under different roots.
    for kind, extra in all_parameter_names(right).items():
        merged = list(names.get(kind, []))
        merged.extend(n for n in extra if n not in merged)
        for n in list(lover[kind]) + list(rover[kind]):
            if n not in merged:
                merged.append(n)
        names[kind] = merged

    mismatches = []
    for kind in ("scalar", "vector", "texture", "switch"):
        log(f"=== {kind.upper()} parameters ===")
        log(f"{'parameter':<48} {'L effective':<26} {'R effective':<26} L/R ovr")
        for name in sorted(names[kind]):
            lv = effective(left, kind, name)
            rv = effective(right, kind, name)
            lo = "O" if name in lover[kind] else "-"
            ro = "O" if name in rover[kind] else "-"
            ok = same(kind, lv, rv)
            flag = "" if ok else "   <== MISMATCH"
            log(f"{name:<48} {fmt(lv):<26} {fmt(rv):<26} {lo}/{ro}{flag}")
            report["parameters"].setdefault(kind, {})[name] = {
                "left": lv,
                "right": rv,
                "left_overridden": lo == "O",
                "right_overridden": ro == "O",
                "match": ok,
            }
            if not ok:
                mismatches.append((kind, name, lv, rv))
        log("")

    mesh = load(FACE_MESH)
    if mesh is not None:
        log("=== Face mesh material slots (eye-related) ===")
        for index, mat in enumerate(prop(mesh, "materials", []) or []):
            slot = str(prop(mat, "material_slot_name"))
            iface = prop(mat, "material_interface")
            path = iface.get_path_name() if iface else "None"
            if "eye" in slot.lower() or "Eye" in path:
                log(f"  [{index}] {slot:<28} -> {path}")
                report["mesh_slots"][slot] = path
        log("")

    log("=== Summary ===")
    if mismatches:
        log(f"RESULT: {len(mismatches)} parameter mismatch(es) between left and right eye")
        for kind, name, lv, rv in mismatches:
            log(f"  {kind:<8} {name:<46} L={fmt(lv):<24} R={fmt(rv)}")
    else:
        log("RESULT: PASS - left and right eye parameters agree")

    with open(saved_path(REPORT_TXT), "w", encoding="utf-8") as handle:
        handle.write("\n".join(_lines) + "\n")
    with open(saved_path(REPORT_JSON), "w", encoding="utf-8") as handle:
        json.dump(report, handle, indent=2)
    unreal.log(f"[EyeMaterial] wrote {saved_path(REPORT_TXT)}")


main()
