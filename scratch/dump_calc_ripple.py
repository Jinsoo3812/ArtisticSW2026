import unreal

material = unreal.load_asset("/Game/New/Water/Realistic_Water/M_Realistic_Water")
prefix = "/Game/New/Water/Realistic_Water/M_Realistic_Water.M_Realistic_Water:"

custom_0 = unreal.load_object(None, f"{prefix}MaterialExpressionCustom_0")
if custom_0:
    unreal.log_warning("=== Custom_0 (CalcRipple) Details ===")
    unreal.log_warning(f"Desc: {custom_0.get_editor_property('description')}")
    unreal.log_warning(f"OutputType: {custom_0.get_editor_property('output_type')}")
    inputs = list(unreal.MaterialEditingLibrary.get_inputs_for_material_expression(material, custom_0))
    for idx, inp in enumerate(inputs):
        if inp:
            unreal.log_warning(f"  Input[{idx}]: {inp.get_name()} ({inp.get_class().get_name()})")
        else:
            unreal.log_warning(f"  Input[{idx}]: None")
    unreal.log_warning(f"FULL CODE:\n{custom_0.get_editor_property('code')}")
