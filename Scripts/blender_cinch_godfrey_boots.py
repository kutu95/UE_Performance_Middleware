"""Blender cinch for Godfrey Loose_Biker_Boots (run inside Blender, not Unreal).

Only vertices on the Loose_Biker_Boots material move. Coat / pants / tank stay.

  blender -b --python blender_cinch_godfrey_boots.py -- --in IN.fbx --out OUT.fbx

Optional: --amount 0.55  (smaller = tighter collar; 1.0 = no change)
"""
from __future__ import annotations

import argparse
import os
import sys

import bpy
from mathutils import Vector


BOOT_TOKENS = ("loose_biker_boots", "biker_boot", "boot")
FOOT_L = ("foot_l", "ball_l", "ankle_fwd_l")
FOOT_R = ("foot_r", "ball_r", "ankle_fwd_r")
SHIN_L = ("calf_l", "calf_knee_l", "thigh_l")
SHIN_R = ("calf_r", "calf_knee_r", "thigh_r")
TOE_L = ("ball_l", "foot_l")
TOE_R = ("ball_r", "foot_r")


def log(msg: str) -> None:
    print(f"[BootCinch] {msg}")


def parse_args():
    argv = sys.argv
    if "--" in argv:
        argv = argv[argv.index("--") + 1 :]
    else:
        argv = []
    parser = argparse.ArgumentParser()
    parser.add_argument("--in", dest="in_fbx", required=True)
    parser.add_argument("--out", dest="out_fbx", required=True)
    parser.add_argument("--amount", type=float, default=0.58)
    parser.add_argument("--collar-start", type=float, default=0.38)
    return parser.parse_args(argv)


def find_bone(arm, names: tuple[str, ...]):
    bones = arm.data.bones
    lower = {b.name.lower(): b for b in bones}
    for name in names:
        bone = bones.get(name) or lower.get(name.lower())
        if bone:
            return bone
    for bone in bones:
        bl = bone.name.lower()
        for name in names:
            if name.lower() in bl:
                return bone
    return None


def bone_head_world(arm, bone) -> Vector:
    return arm.matrix_world @ bone.head_local


def is_boot_mat(mat) -> bool:
    if not mat:
        return False
    name = mat.name.lower()
    return any(tok in name for tok in BOOT_TOKENS)


def boot_vert_indices(mesh) -> set[int]:
    boot_slots = {
        i for i, mat in enumerate(mesh.materials) if is_boot_mat(mat)
    }
    if not boot_slots and mesh.materials:
        # Combined outfit usually names the slot Loose_Biker_Boots.
        for i, mat in enumerate(mesh.materials):
            log(f"  material[{i}]={mat.name if mat else None}")
    indices: set[int] = set()
    if not boot_slots:
        return indices
    for poly in mesh.polygons:
        if poly.material_index in boot_slots:
            indices.update(poly.vertices)
    return indices


def smoothstep(x: float) -> float:
    x = max(0.0, min(1.0, x))
    return x * x * (3.0 - 2.0 * x)


def cinch_mesh(obj, arm, amount: float, collar_start: float) -> int:
    mesh = obj.data
    boot_verts = boot_vert_indices(mesh)
    log(f"  {obj.name}: boot verts={len(boot_verts)} of {len(mesh.vertices)}")
    if len(boot_verts) < 20:
        return 0

    mw = obj.matrix_world
    imw = mw.inverted()

    pairs = []
    for foot_names, shin_names, toe_names in (
        (FOOT_L, SHIN_L, TOE_L),
        (FOOT_R, SHIN_R, TOE_R),
    ):
        foot_b = find_bone(arm, foot_names) if arm else None
        shin_b = find_bone(arm, shin_names) if arm else None
        toe_b = find_bone(arm, toe_names) if arm else None
        if foot_b and shin_b:
            foot = bone_head_world(arm, foot_b)
            shin = bone_head_world(arm, shin_b)
            toe = bone_head_world(arm, toe_b) if toe_b else foot + Vector((1.0, 0.0, 0.0))
            pairs.append((foot, shin, toe))
            log(f"  bones foot={foot_b.name} shin={shin_b.name}")

    if not pairs:
        # No armature: split by object-space X and cinch toward each half centroid.
        worlds = [mw @ mesh.vertices[i].co for i in boot_verts]
        xs = [v.x for v in worlds]
        mid = sum(xs) / max(len(xs), 1)
        left = [v for v in worlds if v.x <= mid]
        right = [v for v in worlds if v.x > mid]

        def centroid(pts):
            acc = Vector((0.0, 0.0, 0.0))
            for p in pts:
                acc += p
            return acc / max(len(pts), 1)

        for pts in (left, right):
            if len(pts) < 10:
                continue
            c = centroid(pts)
            zs = sorted(p.z for p in pts)
            z0, z1 = zs[len(zs) // 10], zs[-1]
            sole = Vector((c.x, c.y, z0))
            top = Vector((c.x, c.y, z1))
            pairs.append((sole, top, sole + Vector((1.0, 0.0, 0.0))))
        log("  fallback axis from vertex bounds (no foot/calf bones)")

    moved = 0
    for idx in boot_verts:
        world = mw @ mesh.vertices[idx].co
        foot, shin, toe = min(pairs, key=lambda p: (world - p[0]).length_squared)
        axis = shin - foot
        if axis.length < 1e-4:
            continue
        axis.normalize()
        rel = world - foot
        t = rel.dot(axis)
        radial = rel - axis * t
        # Project this foot's boot verts for range.
        # Use a per-side cache: compute t_min/t_max lazily via attribute on pair.
        key = id(foot)
        if not hasattr(cinch_mesh, "_range"):
            cinch_mesh._range = {}
        if key not in cinch_mesh._range:
            ts = []
            for j in boot_verts:
                wj = mw @ mesh.vertices[j].co
                fj, sj, _ = min(pairs, key=lambda p: (wj - p[0]).length_squared)
                if id(fj) != key:
                    continue
                aj = sj - fj
                if aj.length < 1e-4:
                    continue
                aj.normalize()
                ts.append((wj - fj).dot(aj))
            cinch_mesh._range[key] = (min(ts), max(ts)) if ts else (0.0, 1.0)
        t_min, t_max = cinch_mesh._range[key]
        span = max(t_max - t_min, 1e-4)
        along = (t - t_min) / span
        if along < 0.12:
            continue  # sole / welt
        collar_w = smoothstep((along - collar_start) / max(1.0 - collar_start, 1e-4))
        shaft_w = smoothstep((along - 0.22) / 0.35) * 0.35
        pull = max(collar_w, shaft_w)
        if pull <= 0.001:
            continue
        scale = 1.0 - pull * (1.0 - amount)
        new_radial = radial * scale
        # Tuck the tongue: verts ahead of the shin, in the collar half, come back a little.
        toe_dir = toe - foot
        if toe_dir.length > 1e-4:
            toe_dir.normalize()
            ahead = max(0.0, radial.dot(toe_dir))
            if along > 0.35 and ahead > 0.4:
                new_radial = new_radial - toe_dir * (ahead * 0.35 * collar_w)
        new_world = foot + axis * t + new_radial
        mesh.vertices[idx].co = imw @ new_world
        moved += 1
    if hasattr(cinch_mesh, "_range"):
        del cinch_mesh._range
    mesh.update()
    return moved


def import_fbx(path: str) -> None:
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    bpy.ops.import_scene.fbx(
        filepath=path,
        ignore_leaf_bones=True,
        automatic_bone_orientation=False,
        use_custom_normals=True,
    )


def export_fbx(path: str) -> None:
    bpy.ops.object.select_all(action="SELECT")
    kwargs = dict(
        filepath=path,
        use_selection=True,
        add_leaf_bones=False,
        bake_anim=False,
        apply_unit_scale=True,
        apply_scale_options="FBX_SCALE_ALL",
        mesh_smooth_type="FACE",
        use_armature_deform_only=True,
        path_mode="COPY",
        embed_textures=False,
        axis_forward="-Y",
        axis_up="Z",
    )
    try:
        bpy.ops.export_scene.fbx(**kwargs)
    except TypeError:
        kwargs.pop("apply_scale_options", None)
        bpy.ops.export_scene.fbx(**kwargs)


def main() -> None:
    args = parse_args()
    in_fbx = os.path.abspath(args.in_fbx)
    out_fbx = os.path.abspath(args.out_fbx)
    if not os.path.isfile(in_fbx):
        raise SystemExit(f"Missing input FBX: {in_fbx}")
    os.makedirs(os.path.dirname(out_fbx), exist_ok=True)
    log(f"in={in_fbx}")
    log(f"out={out_fbx}")
    log(f"amount={args.amount} collar_start={args.collar_start}")

    import_fbx(in_fbx)
    arms = [o for o in bpy.context.scene.objects if o.type == "ARMATURE"]
    meshes = [o for o in bpy.context.scene.objects if o.type == "MESH"]
    arm = arms[0] if arms else None
    log(f"armatures={len(arms)} meshes={len(meshes)}")
    if not meshes:
        raise SystemExit("FBX imported with no mesh")

    moved_total = 0
    for obj in meshes:
        bpy.context.view_layer.objects.active = obj
        moved = cinch_mesh(obj, arm, args.amount, args.collar_start)
        log(f"  moved {moved} verts on {obj.name}")
        moved_total += moved
    if moved_total < 20:
        raise SystemExit("No Loose_Biker_Boots verts found — check material names in the FBX")

    export_fbx(out_fbx)
    if not os.path.isfile(out_fbx) or os.path.getsize(out_fbx) < 1024:
        raise SystemExit("FBX export failed")
    log(f"PASS moved={moved_total} bytes={os.path.getsize(out_fbx)}")


if __name__ == "__main__":
    main()
