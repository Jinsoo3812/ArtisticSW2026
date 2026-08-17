import unreal

material = unreal.load_asset("/Game/New/Water/Realistic_Water/M_Realistic_Water")
prefix = "/Game/New/Water/Realistic_Water/M_Realistic_Water.M_Realistic_Water:"

custom_6 = unreal.load_object(None, f"{prefix}MaterialExpressionCustom_6")
inputs = list(unreal.MaterialEditingLibrary.get_inputs_for_material_expression(material, custom_6))

unreal.log_warning("=== Custom_6 Detailed Parameter Check ===")
for idx, inp in enumerate(inputs):
    if inp:
        cname = inp.get_class().get_name()
        # In UE python, get_editor_property('parameter_name') on MaterialExpressionParameter
        pname = "None"
        try:
            pname = str(inp.get_editor_property("parameter_name"))
        except:
            pass
        unreal.log_warning(f"  Input[{idx}]: {inp.get_name()} ({cname}) -> ParamName='{pname}'")

# Also check Custom_20 inputs to compare
custom_20 = unreal.load_object(None, f"{prefix}MaterialExpressionCustom_20")
inputs_20 = list(unreal.MaterialEditingLibrary.get_inputs_for_material_expression(material, custom_20))
unreal.log_warning("=== Custom_20 Detailed Parameter Check ===")
for idx, inp in enumerate(inputs_20):
    if inp:
        cname = inp.get_class().get_name()
        pname = "None"
        try:
            pname = str(inp.get_editor_property("parameter_name"))
        except:
            pass
        unreal.log_warning(f"  Input[{idx}]: {inp.get_name()} ({cname}) -> ParamName='{pname}'")
