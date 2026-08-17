import unreal

material = unreal.load_asset("/Game/New/Water/Realistic_Water/M_Realistic_Water")
prefix = "/Game/New/Water/Realistic_Water/M_Realistic_Water.M_Realistic_Water:"

custom_6 = unreal.load_object(None, f"{prefix}MaterialExpressionCustom_6")
inputs = list(unreal.MaterialEditingLibrary.get_inputs_for_material_expression(material, custom_6))

unreal.log_warning("=== Custom_6 Parameter Names & Types ===")
for idx, inp in enumerate(inputs):
    if inp:
        cname = inp.get_class().get_name()
        pname = inp.get_editor_property("parameter_name") if hasattr(inp, "parameter_name") else "N/A"
        unreal.log_warning(f"  Input[{idx}]: {inp.get_name()} ({cname}) -> ParamName='{pname}'")
