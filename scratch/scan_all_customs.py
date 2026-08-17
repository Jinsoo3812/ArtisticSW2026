import unreal

material = unreal.load_asset("/Game/New/Water/Realistic_Water/M_Realistic_Water")
prefix = "/Game/New/Water/Realistic_Water/M_Realistic_Water.M_Realistic_Water:"

unreal.log_warning("=== Scanning ALL Custom Nodes in M_Realistic_Water ===")

for i in range(1500):
    cn = unreal.load_object(None, f"{prefix}MaterialExpressionCustom_{i}")
    if cn:
        desc = cn.get_editor_property("description")
        code = str(cn.get_editor_property("code"))
        inputs = list(unreal.MaterialEditingLibrary.get_inputs_for_material_expression(material, cn))
        input_names = [x.get_name() if x else "None" for x in inputs]
        unreal.log_warning(f"[Custom_{i}] Desc='{desc}' Connected Inputs: {input_names}")
        unreal.log_warning(f"   Code snippet:\n{code[:200]}")

# Also check SetMaterialAttributes_2 inputs
set_mat_2 = unreal.load_object(None, f"{prefix}MaterialExpressionSetMaterialAttributes_2")
if set_mat_2:
    inputs = list(unreal.MaterialEditingLibrary.get_inputs_for_material_expression(material, set_mat_2))
    unreal.log_warning("=== SetMaterialAttributes_2 Current Inputs ===")
    for idx, inp in enumerate(inputs):
        if inp:
            unreal.log_warning(f"  Pin[{idx}] <- {inp.get_name()} ({inp.get_class().get_name()})")
        else:
            unreal.log_warning(f"  Pin[{idx}] <- None")

