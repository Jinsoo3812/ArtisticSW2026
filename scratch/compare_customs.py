import unreal

material = unreal.load_asset("/Game/New/Water/Realistic_Water/M_Realistic_Water")
prefix = "/Game/New/Water/Realistic_Water/M_Realistic_Water.M_Realistic_Water:"

custom_6 = unreal.load_object(None, f"{prefix}MaterialExpressionCustom_6")
if custom_6:
    unreal.log_warning("=== Custom_6 (SW_Kelvin_Wake_Normal) Detailed ===")
    unreal.log_warning(f"Desc: {custom_6.get_editor_property('description')}")
    unreal.log_warning(f"Code:\n{custom_6.get_editor_property('code')}")
    inputs = list(unreal.MaterialEditingLibrary.get_inputs_for_material_expression(material, custom_6))
    for idx, inp in enumerate(inputs):
        if inp:
            unreal.log_warning(f"  Input[{idx}] <- {inp.get_name()} ({inp.get_class().get_name()})")
        else:
            unreal.log_warning(f"  Input[{idx}] <- None")

# Also check Custom_20 (Kelvin WPO) to compare inputs
custom_20 = unreal.load_object(None, f"{prefix}MaterialExpressionCustom_20")
if custom_20:
    unreal.log_warning("=== Custom_20 (Kelvin WPO) Detailed ===")
    inputs = list(unreal.MaterialEditingLibrary.get_inputs_for_material_expression(material, custom_20))
    for idx, inp in enumerate(inputs):
        if inp:
            unreal.log_warning(f"  Input[{idx}] <- {inp.get_name()} ({inp.get_class().get_name()})")
        else:
            unreal.log_warning(f"  Input[{idx}] <- None")
