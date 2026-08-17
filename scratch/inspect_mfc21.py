import unreal

material = unreal.load_asset("/Game/New/Water/Realistic_Water/M_Realistic_Water")
prefix = "/Game/New/Water/Realistic_Water/M_Realistic_Water.M_Realistic_Water:"

def inspect_mfc(mfc_name):
    mfc = unreal.load_object(None, f"{prefix}{mfc_name}")
    if mfc:
        fn = mfc.get_editor_property("material_function")
        inps = list(unreal.MaterialEditingLibrary.get_inputs_for_material_expression(material, mfc))
        inp_names = [x.get_name() if x else 'None' for x in inps]
        unreal.log_warning(f"=== {mfc_name} ({fn.get_name() if fn else 'None'}) ===")
        unreal.log_warning(f"   Inputs: {inp_names}")
        for inp in inps:
            if inp:
                cname = inp.get_class().get_name()
                if "Custom" in cname:
                    unreal.log_warning(f"     Custom: {inp.get_name()} Desc='{inp.get_editor_property('description')}'")
                    unreal.log_warning(f"     Code:\n{inp.get_editor_property('code')}")

inspect_mfc("MaterialExpressionMaterialFunctionCall_21")
inspect_mfc("MaterialExpressionMaterialFunctionCall_23")

# Also find newly added custom nodes (check all custom nodes again)
for i in range(100):
    cn = unreal.load_object(None, f"{prefix}MaterialExpressionCustom_{i}")
    if cn:
        desc = cn.get_editor_property("description")
        unreal.log_warning(f"Custom_{i}: Desc='{desc}'")
