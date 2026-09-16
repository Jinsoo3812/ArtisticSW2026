import unreal


ROOT = "/Game/Ladder"

for asset_path in unreal.EditorAssetLibrary.list_assets(ROOT, recursive=True, include_folder=False):
    asset = unreal.EditorAssetLibrary.load_asset(asset_path)
    if not asset:
        unreal.log_warning(f"LADDER_ASSET load-failed path={asset_path}")
        continue
    unreal.log_warning(
        f"LADDER_ASSET path={asset_path} class={asset.get_class().get_name()}"
    )
    if isinstance(asset, unreal.StaticMesh):
        materials = asset.get_editor_property("static_materials")
        unreal.log_warning(f"LADDER_MESH slots={len(materials)}")
        for index, slot in enumerate(materials):
            material = slot.get_editor_property("material_interface")
            slot_name = slot.get_editor_property("material_slot_name")
            unreal.log_warning(
                f"LADDER_SLOT index={index} name={slot_name} material={material.get_path_name() if material else 'None'}"
            )
    elif isinstance(asset, unreal.MaterialInstanceConstant):
        parent = asset.get_editor_property("parent")
        unreal.log_warning(
            f"LADDER_INSTANCE parent={parent.get_path_name() if parent else 'None'}"
        )
    elif isinstance(asset, unreal.Texture2D):
        unreal.log_warning(
            "LADDER_TEXTURE "
            f"name={asset.get_name()} srgb={asset.get_editor_property('srgb')} "
            f"compression={asset.get_editor_property('compression_settings')} "
            f"flip_green={asset.get_editor_property('flip_green_channel')}"
        )
