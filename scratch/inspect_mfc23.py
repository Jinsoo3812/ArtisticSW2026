import unreal

material = unreal.load_asset("/Game/New/Water/Realistic_Water/M_Realistic_Water")
prefix = "/Game/New/Water/Realistic_Water/M_Realistic_Water.M_Realistic_Water:"

mfc_23 = unreal.load_object(None, f"{prefix}MaterialExpressionMaterialFunctionCall_23")
if mfc_23:
    fn = mfc_23.get_editor_property("material_function")
    unreal.log_warning(f"MaterialFunctionCall_23 Function: {fn.get_name() if fn else 'None'}")
    inps = list(unreal.MaterialEditingLibrary.get_inputs_for_material_expression(material, mfc_23))
    unreal.log_warning(f"MFC_23 Inputs count={len(inps)}: {[x.get_name() if x else 'None' for x in inps]}")

# Also check SetMaterialAttributes_2 attribute mapping
set_mat_2 = unreal.load_object(None, f"{prefix}MaterialExpressionSetMaterialAttributes_2")
if set_mat_2:
    attrs = set_mat_2.get_editor_property("attribute_set_types")
    unreal.log_warning(f"SetMaterialAttributes_2 attribute_set_types: {len(attrs)} items")
    # Check what each attribute is
    # In UE FMaterialAttributesInput has PropertyGuid
    # Let's print the pin indices and corresponding property names if possible
    # Or print the names of inputs
    inputs = list(unreal.MaterialEditingLibrary.get_inputs_for_material_expression(material, set_mat_2))
    for idx, inp in enumerate(inputs):
        if inp:
            unreal.log_warning(f"SetMat_2 Pin[{idx}] -> {inp.get_name()} ({inp.get_class().get_name()})")

