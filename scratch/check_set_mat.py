import unreal

material = unreal.load_asset("/Game/New/Water/Realistic_Water/M_Realistic_Water")
prefix = "/Game/New/Water/Realistic_Water/M_Realistic_Water.M_Realistic_Water:"

set_mat = unreal.load_object(None, f"{prefix}MaterialExpressionSetMaterialAttributes_2")
if set_mat:
    props = set_mat.get_editor_property("attribute_set_types")
    unreal.log_warning(f"SetMaterialAttributes_2 attribute_set_types: {[str(x) for x in props]}")
    inputs = list(unreal.MaterialEditingLibrary.get_inputs_for_material_expression(material, set_mat))
    unreal.log_warning(f"SetMaterialAttributes_2 inputs count={len(inputs)}: {[x.get_name() if x else 'None' for x in inputs]}")

# Check if SetMaterialAttributes_2 is connected to Material Root or another node
for i in range(900):
    for class_name in ["MaterialExpressionMaterialFunctionCall", "MaterialExpressionSetMaterialAttributes"]:
        obj = unreal.load_object(None, f"{prefix}{class_name}_{i}")
        if obj and obj != set_mat:
            inputs = list(unreal.MaterialEditingLibrary.get_inputs_for_material_expression(material, obj))
            if set_mat in inputs:
                unreal.log_warning(f"SetMaterialAttributes_2 -> {obj.get_name()} ({class_name})")
