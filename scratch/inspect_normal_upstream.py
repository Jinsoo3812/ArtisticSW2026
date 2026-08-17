import unreal

material = unreal.load_asset("/Game/New/Water/Realistic_Water/M_Realistic_Water")
prefix = "/Game/New/Water/Realistic_Water/M_Realistic_Water.M_Realistic_Water:"

# Check SetMaterialAttributes_0 and SetMaterialAttributes_1
for sm_name in ["MaterialExpressionSetMaterialAttributes_0", "MaterialExpressionSetMaterialAttributes_1"]:
    sm = unreal.load_object(None, f"{prefix}{sm_name}")
    if sm:
        inputs = list(unreal.MaterialEditingLibrary.get_inputs_for_material_expression(material, sm))
        unreal.log_warning(f"=== {sm_name} Inputs ===")
        for idx, inp in enumerate(inputs):
            if inp:
                desc = ""
                if "Custom" in inp.get_class().get_name():
                    desc = f"Desc='{inp.get_editor_property('description')}'"
                unreal.log_warning(f"  Pin[{idx}] <- {inp.get_name()} ({inp.get_class().get_name()}) {desc}")

# Find what uses Custom_5
custom_5 = unreal.load_object(None, f"{prefix}MaterialExpressionCustom_5")
unreal.log_warning("=== Searching users of Custom_5 ===")
for i in range(1500):
    for cname in ["MaterialExpressionSetMaterialAttributes", "MaterialExpressionMaterialFunctionCall", "MaterialExpressionAdd", "MaterialExpressionMultiply", "MaterialExpressionLinearInterpolate", "MaterialExpressionReroute", "MaterialExpressionStaticSwitchParameter", "MaterialExpressionTransform"]:
        obj = unreal.load_object(None, f"{prefix}{cname}_{i}")
        if obj and obj != custom_5:
            inps = list(unreal.MaterialEditingLibrary.get_inputs_for_material_expression(material, obj))
            if custom_5 in inps:
                unreal.log_warning(f"Custom_5 is used by -> {obj.get_name()} ({cname})")

