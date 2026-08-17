import unreal

material = unreal.load_asset("/Game/New/Water/Realistic_Water/M_Realistic_Water")
prefix = "/Game/New/Water/Realistic_Water/M_Realistic_Water.M_Realistic_Water:"

set_mat = unreal.load_object(None, f"{prefix}MaterialExpressionSetMaterialAttributes_2")
if set_mat:
    props = set_mat.get_editor_property("attribute_set_types")
    unreal.log_warning(f"SetMaterialAttributes_2 attribute_set_types: {[str(x) for x in props]}")
    inputs = list(unreal.MaterialEditingLibrary.get_inputs_for_material_expression(material, set_mat))
    unreal.log_warning(f"SetMaterialAttributes_2 inputs count={len(inputs)}: {[x.get_name() if x else 'None' for x in inputs]}")

add1 = unreal.load_object(None, f"{prefix}MaterialExpressionAdd_1")
if add1:
    inputs = list(unreal.MaterialEditingLibrary.get_inputs_for_material_expression(material, add1))
    unreal.log_warning(f"MaterialExpressionAdd_1 inputs: {[x.get_name() if x else 'None' for x in inputs]}")
