import unreal

paths = [
    "/Game/New/Water/Realistic_Water/M_Realistic_Water",
    "/Game/New/Water/Realistic_Water/M_Realistic_Water_Ocean"
]

for p in paths:
    asset = unreal.load_asset(p)
    if not asset:
        unreal.log_error(f"FAILED to load: {p}")
        continue
    cname = asset.get_class().get_name()
    unreal.log_warning(f"=== Asset: {p} ({cname}) ===")
    if isinstance(asset, unreal.MaterialInstance):
        parent = asset.get_editor_property('parent')
        unreal.log_warning(f"  Parent: {parent.get_path_name() if parent else 'None'}")
    
    scalars = unreal.MaterialEditingLibrary.get_scalar_parameter_names(asset)
    unreal.log_warning(f"  Scalar parameters ({len(scalars)}): {list(scalars)}")
    textures = unreal.MaterialEditingLibrary.get_texture_parameter_names(asset)
    unreal.log_warning(f"  Texture parameters ({len(textures)}): {list(textures)}")
