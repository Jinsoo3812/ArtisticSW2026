import unreal

mat_path = "/Game/New/Water/Realistic_Water/M_Realistic_Water"
mat = unreal.load_asset(mat_path)

scalars = unreal.MaterialEditingLibrary.get_scalar_parameter_names(mat)
for s in scalars:
    name = str(s)
    if any(k in name.lower() for k in ["rough", "specular", "fresnel", "refract", "normal", "godot", "v2", "v3", "foam"]):
        val = unreal.MaterialEditingLibrary.get_material_default_scalar_parameter_value(mat, s)
        unreal.log_warning(f"  {name} = {val}")
