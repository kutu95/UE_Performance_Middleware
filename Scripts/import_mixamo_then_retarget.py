"""Import Mixamo on its own skeleton, then IK-retarget to MetaHuman m_med_nrw (UE 5.6).

Run in editor: Tools > Execute Python Script

Optional / deferred: Mixamo talking is a placeholder until custom Kristofer montages exist.
After retarget, recreate As_Godfrey_Talking_Anim_Montage from the output sequence (DefaultSlot).

UE 5.6 API: IKRetargetFactory (not IKRetargeterFactory), IKRetargetBatchOperation.duplicate_and_retarget (static).
"""
from __future__ import annotations

import unreal

MIXAMO_FBX_WITH_SKIN = "D:/Downloads/Animations/Talking.fbx"
MIXAMO_CONTENT_ROOT = "/Game/Godfrey/Animation/Mixamo"
MIXAMO_ASSET_NAME = "Mixamo_Talking"
TARGET_IK_RIG_PATH = "/Game/MetaHumans/Common/Common/Animation/Retargeting/IK_MetaHuman_m_med_nrw"
TARGET_PREVIEW_MESH_PATH = "/Game/MetaHumans/Common/Common/m_med_nrw_body_preview"
RETARGETER_PATH = "/Game/Godfrey/Animation/Retargeting/RTG_Mixamo_To_MetaHuman"
OUTPUT_ANIM_DIR = "/Game/Godfrey/Animation/Retargeted"


def log(msg: str) -> None:
    unreal.log(f"[MixamoRetarget] {msg}")


def ensure_directory(path: str) -> None:
    if not unreal.EditorAssetLibrary.does_directory_exist(path):
        unreal.EditorAssetLibrary.make_directory(path)


def build_fbx_import_ui(*, skeleton: unreal.Skeleton | None, import_mesh: bool, import_anim: bool) -> unreal.FbxImportUI:
    ui = unreal.FbxImportUI()
    ui.set_editor_property("import_mesh", import_mesh)
    ui.set_editor_property("import_animations", import_anim)
    ui.set_editor_property("import_materials", False)
    ui.set_editor_property("import_textures", False)
    ui.set_editor_property("import_rigid_mesh", False)
    ui.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_SKELETAL_MESH)
    ui.set_editor_property("original_import_type", unreal.FBXImportType.FBXIT_SKELETAL_MESH)
    ui.set_editor_property("automated_import_should_detect_type", False)
    ui.set_editor_property("skeleton", skeleton)
    ui.set_editor_property("create_physics_asset", False)
    ui.set_editor_property("override_full_name", True)
    return ui


def import_fbx(filename: str, destination: str, asset_name: str, ui: unreal.FbxImportUI) -> list[str]:
    ensure_directory(destination)
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", filename)
    task.set_editor_property("destination_path", destination)
    task.set_editor_property("destination_name", asset_name)
    task.set_editor_property("automated", True)
    task.set_editor_property("save", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("async_", False)
    task.set_editor_property("options", ui)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    paths = task.get_editor_property("imported_object_paths") or []
    log(f"Imported {filename} -> {paths}")
    return paths


def find_first_asset(paths: list[str], cls) -> unreal.Object | None:
    for path in paths:
        package = path.split(".")[0] if "." in path else path
        asset = unreal.load_asset(package)
        if isinstance(asset, cls):
            return asset
    return None


def import_mixamo_with_own_skeleton() -> tuple[unreal.SkeletalMesh, unreal.AnimSequence]:
    ui = build_fbx_import_ui(skeleton=None, import_mesh=True, import_anim=True)
    paths = import_fbx(MIXAMO_FBX_WITH_SKIN, MIXAMO_CONTENT_ROOT, MIXAMO_ASSET_NAME, ui)
    if not paths:
        raise RuntimeError("Mixamo import produced no assets.")

    mesh = find_first_asset(paths, unreal.SkeletalMesh)
    anim = find_first_asset(paths, unreal.AnimSequence)
    if not mesh or not anim:
        raise RuntimeError(f"Expected skeletal mesh + anim sequence in import results: {paths}")

    skeleton = mesh.get_editor_property("skeleton")
    if not skeleton:
        raise RuntimeError("Imported mesh has no skeleton.")
    if "metahuman_base_skel" in skeleton.get_path_name():
        raise RuntimeError("Import bound to MetaHuman skeleton — retry with Create New Skeleton.")

    return mesh, anim


def ensure_mixamo_ik_rig(preview_mesh: unreal.SkeletalMesh) -> unreal.IKRigDefinition:
    ik_path = f"{MIXAMO_CONTENT_ROOT}/IK_Mixamo_Talking"
    if unreal.EditorAssetLibrary.does_asset_exist(ik_path):
        return unreal.load_asset(ik_path)

    factory = unreal.IKRigDefinitionFactory()
    ik = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        "IK_Mixamo_Talking", MIXAMO_CONTENT_ROOT, unreal.IKRigDefinition, factory
    )
    if not ik:
        raise RuntimeError("Failed to create Mixamo IK Rig asset.")

    controller = unreal.IKRigController.get_controller(ik)
    controller.set_skeletal_mesh(preview_mesh)
    for chain, bone in (
        ("Root", "mixamorig:Hips"),
        ("Spine", "mixamorig:Spine"),
        ("LeftArm", "mixamorig:LeftArm"),
        ("RightArm", "mixamorig:RightArm"),
        ("LeftLeg", "mixamorig:LeftUpLeg"),
        ("RightLeg", "mixamorig:RightUpLeg"),
        ("Head", "mixamorig:Head"),
    ):
        controller.add_retarget_chain_from_skeleton(chain, bone)

    unreal.EditorAssetLibrary.save_loaded_asset(ik)
    return ik


def ensure_retargeter(source_ik: unreal.IKRigDefinition, target_ik: unreal.IKRigDefinition) -> unreal.IKRetargeter:
    ensure_directory("/Game/Godfrey/Animation/Retargeting")
    if unreal.EditorAssetLibrary.does_asset_exist(RETARGETER_PATH):
        rtg = unreal.load_asset(RETARGETER_PATH)
    else:
        factory = unreal.IKRetargetFactory()
        rtg = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            "RTG_Mixamo_To_MetaHuman",
            "/Game/Godfrey/Animation/Retargeting",
            unreal.IKRetargeter,
            factory,
        )
        if not rtg:
            raise RuntimeError("Failed to create IK Retargeter asset (use unreal.IKRetargetFactory in UE 5.6).")

    controller = unreal.IKRetargeterController.get_controller(rtg)
    controller.set_ik_rig(unreal.RetargetSourceOrTarget.SOURCE, source_ik)
    controller.set_ik_rig(unreal.RetargetSourceOrTarget.TARGET, target_ik)
    controller.auto_map_chains(unreal.AutoMapChainType.FUZZY, True)
    unreal.EditorAssetLibrary.save_loaded_asset(rtg)
    log(f"Retargeter ready: {RETARGETER_PATH}")
    return rtg


def retarget_sequence(
    source_anim: unreal.AnimSequence,
    source_mesh: unreal.SkeletalMesh,
    target_mesh: unreal.SkeletalMesh,
    retargeter: unreal.IKRetargeter,
) -> list[unreal.AssetData]:
    ensure_directory(OUTPUT_ANIM_DIR)
    asset_data = unreal.EditorAssetLibrary.find_asset_data(source_anim.get_path_name())
    if not asset_data.is_valid():
        raise RuntimeError(f"Could not resolve asset data for {source_anim.get_path_name()}")

    results = unreal.IKRetargetBatchOperation.duplicate_and_retarget(
        [asset_data],
        source_mesh,
        target_mesh,
        retargeter,
        "",
        "",
        "",
        "_MetaHuman",
        True,
    )
    log(f"duplicate_and_retarget created {len(results)} asset(s)")
    for item in results:
        log(f"  -> {item.get_full_name()}")
    return results


def main() -> None:
    target_ik = unreal.load_asset(TARGET_IK_RIG_PATH)
    target_mesh = unreal.load_asset(TARGET_PREVIEW_MESH_PATH)
    if not target_ik or not target_mesh:
        raise RuntimeError("Missing MetaHuman IK rig or preview mesh.")

    mixamo_mesh, mixamo_anim = import_mixamo_with_own_skeleton()
    source_ik = ensure_mixamo_ik_rig(mixamo_mesh)
    retargeter = ensure_retargeter(source_ik, target_ik)
    results = retarget_sequence(mixamo_anim, mixamo_mesh, target_mesh, retargeter)
    if not results:
        log("Retarget finished with no new assets; check Output Log and Content/Godfrey/Animation/Retargeted.")
        return

    log(
        "DONE. Open the *_MetaHuman sequence in Retargeted/, verify on Kristofer mesh, "
        "then recreate As_Godfrey_Talking_Anim_Montage (DefaultSlot) or swap SpeakingIdleMontage."
    )


if __name__ == "__main__":
    main()
