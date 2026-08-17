import unreal

material = unreal.load_asset("/Game/New/Water/Realistic_Water/M_Realistic_Water")
prefix = "/Game/New/Water/Realistic_Water/M_Realistic_Water.M_Realistic_Water:"

# Check Custom_16 (CalcOceanFoam) inputs and where it gets slope/curvature/normal
custom_16 = unreal.load_object(None, f"{prefix}MaterialExpressionCustom_16")
if custom_16:
    unreal.log_warning("=== Custom_16 (CalcOceanFoam) ===")
    unreal.log_warning(f"Code:\n{custom_16.get_editor_property('code')}")
    inputs = list(unreal.MaterialEditingLibrary.get_inputs_for_material_expression(material, custom_16))
    for idx, inp in enumerate(inputs):
        if inp:
            unreal.log_warning(f"  Input[{idx}] <- {inp.get_name()} ({inp.get_class().get_name()})")

# Check Custom_2 and Custom_3
for cn_name in ["MaterialExpressionCustom_2", "MaterialExpressionCustom_3"]:
    cn = unreal.load_object(None, f"{prefix}{cn_name}")
    if cn:
        unreal.log_warning(f"=== {cn_name} ===")
        unreal.log_warning(f"Desc: {cn.get_editor_property('description')}")
        unreal.log_warning(f"Code:\n{cn.get_editor_property('code')}")
