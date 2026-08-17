import unreal

material = unreal.load_asset("/Game/New/Water/Realistic_Water/M_Realistic_Water")
prefix = "/Game/New/Water/Realistic_Water/M_Realistic_Water.M_Realistic_Water:"

# Inspect Custom_0 (CalcRipple)
custom_0 = unreal.load_object(None, f"{prefix}MaterialExpressionCustom_0")
if custom_0:
    unreal.log_warning(f"=== Custom_0 (CalcRipple) ===")
    unreal.log_warning(f"Desc: {custom_0.get_editor_property('description')}")
    unreal.log_warning(f"Code:\n{custom_0.get_editor_property('code')}")
    
    # Check outputs of Custom_0
    for i in range(500):
        for cname in ["MaterialExpressionAdd", "MaterialExpressionMultiply", "MaterialExpressionAppendVector", "MaterialExpressionSetMaterialAttributes", "MaterialExpressionMaterialFunctionCall"]:
            obj = unreal.load_object(None, f"{prefix}{cname}_{i}")
            if obj and obj != custom_0:
                inps = list(unreal.MaterialEditingLibrary.get_inputs_for_material_expression(material, obj))
                if custom_0 in inps:
                    unreal.log_warning(f"Custom_0 (CalcRipple) is connected to -> {obj.get_name()} ({cname})")

