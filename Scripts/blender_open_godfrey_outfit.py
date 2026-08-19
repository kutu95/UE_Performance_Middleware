"""Open the Godfrey outfit FBX in a Blender GUI session.

Blender 5.2's FBX importer calls object.mode_set and fails if the scene is empty.
This keeps the default cube long enough to import, then deletes Cube/Camera/Light.

  blender.exe --python blender_open_godfrey_outfit.py
"""
from __future__ import annotations

import os

import bpy

FBX = os.path.normpath(
    os.path.join(
        os.path.dirname(os.path.abspath(__file__)),
        "..",
        "Saved",
        "BootCinch",
        "MHC_CaptainGodfrey_Outfits.fbx",
    )
)


def _ensure_active_object() -> None:
    if bpy.context.view_layer.objects.active is not None:
        return
    bpy.ops.mesh.primitive_cube_add()


def _override():
    win = bpy.context.window_manager.windows[0]
    area = next((a for a in win.screen.areas if a.type == "VIEW_3D"), None)
    region = next((r for r in area.regions if r.type == "WINDOW"), None) if area else None
    return bpy.context.temp_override(window=win, area=area, region=region)


def main() -> None:
    if not os.path.isfile(FBX):
        raise RuntimeError(f"Missing {FBX}")
    _ensure_active_object()
    with _override():
        bpy.ops.import_scene.fbx(
            filepath=FBX,
            ignore_leaf_bones=True,
            automatic_bone_orientation=False,
            use_custom_normals=True,
        )
    for name in ("Cube", "Camera", "Light"):
        obj = bpy.data.objects.get(name)
        if obj:
            bpy.data.objects.remove(obj, do_unlink=True)
    print(f"[BootCinch] opened {FBX}")


if __name__ == "__main__":
    main()
