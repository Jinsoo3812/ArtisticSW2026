import unreal

material = unreal.load_asset("/Game/New/Water/Realistic_Water/M_Realistic_Water")
prefix = "/Game/New/Water/Realistic_Water/M_Realistic_Water.M_Realistic_Water:"

# Trace inputs of Custom_16 (CalcOceanFoam)
custom_16 = unreal.load_object(None, f"{prefix}MaterialExpressionCustom_16")
inputs = list(unreal.MaterialEditingLibrary.get_inputs_for_material_expression(material, custom_16))
unreal.log_warning("=== Custom_16 (CalcOceanFoam) Input Trace ===")
for idx, inp in enumerate(inputs):
    if inp:
        nname = inp.get_name()
        cname = inp.get_class().get_name()
        pname = inp.get_editor_property("parameter_name") if hasattr(inp, "parameter_name") else ""
        desc = inp.get_editor_property("description") if hasattr(inp, "description") else ""
        unreal.log_warning(f"  Input[{idx}]: {nname} ({cname}) Param='{pname}' Desc='{desc}'")
        # If it's a mask or add, trace its input
        sub_inps = list(unreal.MaterialEditingLibrary.get_inputs_for_material_expression(material, inp))
        for s_idx, s_inp in enumerate(sub_inps):
            if s_inp:
                s_desc = s_inp.get_editor_property("description") if hasattr(s_inp, "description") else ""
                s_pname = s_inp.get_editor_property("parameter_name") if hasattr(s_inp, "parameter_name") else ""
                unreal.log_warning(f"     -> Sub[{s_idx}]: {s_inp.get_name()} ({s_inp.get_class().get_name()}) Param='{s_pname}' Desc='{s_desc}'")

# Also check Water Mesh LOD distance and WPO limits
unreal.log_warning("=== Checking Water Material Instance Parameters ===")
mi = unreal.load_asset("/Game/New/Water/Realistic_Water/M_Realistic_Water_Ocean")
if mi:
    unreal.log_warning(f"Loaded {mi.get_name()}")
    # Check scalar parameters on MI
    s_params = mi.get_editor_property("scalar_parameter_values")
    for sp in s_params:
        pname = sp.get_editor_property("parameter_info").get_editor_property("name")
        val = sp.get_editor_property("parameter_value")
        if any(k in str(pname).lower() for k in ["foam", "fade", "dist", "depth", "bound", "range", "far", "bias", "slope", "crest", "lod"]):
            unreal.log_warning(f"  MI ScalarParam: '{pname}' = {val}")

