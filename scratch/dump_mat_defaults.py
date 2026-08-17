import unreal

mat_path = "/Game/New/Water/Realistic_Water/M_Realistic_Water"
mat = unreal.load_asset(mat_path)

scalars = unreal.MaterialEditingLibrary.get_scalar_parameter_names(mat)
for s in scalars:
    val = unreal.MaterialEditingLibrary.get_material_default_scalar_parameter_value(mat, s)
    unreal.log_warning(f"  Scalar Default: {s} = {val}")

vectors = unreal.MaterialEditingLibrary.get_vector_parameter_names(mat)
for v in vectors:
    val = unreal.MaterialEditingLibrary.get_material_default_vector_parameter_value(mat, v)
    unreal.log_warning(f"  Vector Default: {v} = ({val.r:.2f}, {val.g:.2f}, {val.b:.2f}, {val.a:.2f})")
