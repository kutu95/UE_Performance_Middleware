"""Re-export a Godfrey outfit FBX without Blender leaf bones (_end).

Blender's default FBX export adds *_end bones. Those make the mesh incompatible
with metahuman_base_skel. This script imports, deletes leftover _end bones, and
writes the same path (or --out) with add_leaf_bones=False.

  blender.exe -b --python blender_reexport_mh_outfit_fbx.py -- --in IN.fbx --out OUT.fbx
"""
from __future__ import annotations

import argparse
import os
import sys

import bpy


def log(msg: str) -> None:
    print(f"[ReexportMH] {msg}")


def parse_args():
    argv = sys.argv
    if "--" in argv:
        argv = argv[argv.index("--") + 1 :]
    else:
        argv = []
    parser = argparse.ArgumentParser()
    parser.add_argument("--in", dest="in_fbx", required=True)
    parser.add_argument("--out", dest="out_fbx", required=True)
    return parser.parse_args(argv)


def strip_end_bones(arm) -> int:
    bpy.context.view_layer.objects.active = arm
    bpy.ops.object.mode_set(mode="EDIT")
    removed = 0
    for bone in list(arm.data.edit_bones):
        name = bone.name
        if name.endswith("_end") or name.endswith("_End"):
            arm.data.edit_bones.remove(bone)
            removed += 1
    bpy.ops.object.mode_set(mode="OBJECT")
    return removed


def main() -> None:
    args = parse_args()
    in_fbx = os.path.abspath(args.in_fbx)
    out_fbx = os.path.abspath(args.out_fbx)
    if not os.path.isfile(in_fbx):
        raise SystemExit(f"Missing {in_fbx}")

    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    bpy.ops.import_scene.fbx(
        filepath=in_fbx,
        ignore_leaf_bones=True,
        automatic_bone_orientation=False,
        use_custom_normals=True,
    )

    arms = [o for o in bpy.context.scene.objects if o.type == "ARMATURE"]
    meshes = [o for o in bpy.context.scene.objects if o.type == "MESH"]
    log(f"armatures={len(arms)} meshes={len(meshes)}")
    if not arms or not meshes:
        raise SystemExit("FBX has no armature/mesh")

    stripped = 0
    for arm in arms:
        stripped += strip_end_bones(arm)
    log(f"stripped _end bones: {stripped}")

    bpy.ops.object.select_all(action="SELECT")
    kwargs = dict(
        filepath=out_fbx,
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

    if not os.path.isfile(out_fbx) or os.path.getsize(out_fbx) < 1024:
        raise SystemExit("Export failed")
    log(f"PASS {out_fbx} bytes={os.path.getsize(out_fbx)}")


if __name__ == "__main__":
    main()
