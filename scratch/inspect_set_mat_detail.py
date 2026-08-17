import unreal

material = unreal.load_asset("/Game/New/Water/Realistic_Water/M_Realistic_Water")
prefix = "/Game/New/Water/Realistic_Water/M_Realistic_Water.M_Realistic_Water:"

# Let's inspect SetMaterialAttributes_2 in detail
set_mat_2 = unreal.load_object(None, f"{prefix}MaterialExpressionSetMaterialAttributes_2")
set_mat_0 = unreal.load_object(None, f"{prefix}MaterialExpressionSetMaterialAttributes_0")
set_mat_1 = unreal.load_object(None, f"{prefix}MaterialExpressionSetMaterialAttributes_1")

def inspect_set_mat(sm, name):
    if not sm:
        return
    unreal.log_warning(f"=== {name} ===")
    inputs = list(unreal.MaterialEditingLibrary.get_inputs_for_material_expression(material, sm))
    for idx, inp in enumerate(inputs):
        if inp:
            unreal.log_warning(f"  Pin[{idx}] <- {inp.get_name()} ({inp.get_class().get_name()})")
            if "Custom" in inp.get_class().get_name():
                unreal.log_warning(f"     Custom Desc: {inp.get_editor_property('description')}")
        else:
            unreal.log_warning(f"  Pin[{idx}] <- None")

inspect_set_mat(set_mat_2, "SetMaterialAttributes_2")
inspect_set_mat(set_mat_0, "SetMaterialAttributes_0")
inspect_set_mat(set_mat_1, "SetMaterialAttributes_1")

# Check Custom_5 (Calc_Normal_Godot) and Custom_20 (SW Kelvin Wake M7 Golden Event WPO)
custom_5 = unreal.load_object(None, f"{prefix}MaterialExpressionCustom_5")
if custom_5:
    unreal.log_warning(f"Custom_5 (Calc_Normal_Godot) Code:\n{custom_5.get_editor_property('code')}")

custom_20 = unreal.load_object(None, f"{prefix}MaterialExpressionCustom_20")
if custom_20:
    unreal.log_warning(f"Custom_20 (Kelvin WPO) Code:\n{custom_20.get_editor_property('code')}")

# Trace outputs of Custom_5 and Custom_20
for i in range(1500):
    for cname in ["MaterialExpressionAdd", "MaterialExpressionMultiply", "MaterialExpressionSetMaterialAttributes", "MaterialExpressionMaterialFunctionCall"]:
        obj = unreal.load_object(None, f"{prefix}{cname}_{i}")
        if obj:
            inps = list(unreal.MaterialEditingLibrary.get_inputs_for_material_expression(material, obj))
            if custom_5 in inps:
                unreal.log_warning(f"Custom_5 (Normal) connects to -> {obj.get_name()} ({cname})")
            if custom_20 in inps:
                unreal.log_warning(f"Custom_20 (Kelvin WPO) connects to -> {obj.get_name()} ({cname})")
