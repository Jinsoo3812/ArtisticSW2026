import unreal

material = unreal.load_asset("/Game/New/Water/Realistic_Water/M_Realistic_Water")
prefix = "/Game/New/Water/Realistic_Water/M_Realistic_Water.M_Realistic_Water:"

unreal.log_warning("=== Deep Inspection of Gerstner Waves & Foam ===")

# Find Gerstner Wave material function calls and comments
for i in range(1500):
    for cname in ["MaterialExpressionMaterialFunctionCall", "MaterialExpressionCustom", "MaterialExpressionComment"]:
        obj = unreal.load_object(None, f"{prefix}{cname}_{i}")
        if obj:
            if "MaterialFunctionCall" in cname:
                fn = obj.get_editor_property("material_function")
                fname = fn.get_name() if fn else ""
                if any(k in fname.lower() for k in ["gerstner", "wave", "height", "water", "foam", "wpo"]):
                    inputs = list(unreal.MaterialEditingLibrary.get_inputs_for_material_expression(material, obj))
                    input_names = [x.get_name() if x else "None" for x in inputs]
                    unreal.log_warning(f"[FuncCall: {obj.get_name()}] Func='{fname}' Inputs={input_names}")
            elif "Comment" in cname:
                text = str(obj.get_editor_property("text"))
                if any(k in text.lower() for k in ["gerstner", "wave", "foam", "wpo", "steepness", "height"]):
                    unreal.log_warning(f"[Comment: {obj.get_name()}] Text='{text}'")

# Check all Vector and Scalar parameters connected to Custom_16 (CalcOceanFoam)
custom_16 = unreal.load_object(None, f"{prefix}MaterialExpressionCustom_16")
if custom_16:
    inputs = list(unreal.MaterialEditingLibrary.get_inputs_for_material_expression(material, custom_16))
    for idx, inp in enumerate(inputs):
        if inp:
            cname = inp.get_class().get_name()
            val = "N/A"
            if "Constant3Vector" in cname or "Constant4Vector" in cname or "VectorParameter" in cname:
                val = str(inp.get_editor_property("constant") if hasattr(inp, "constant") else inp.get_editor_property("default_value"))
            elif "ScalarParameter" in cname:
                val = str(inp.get_editor_property("default_value"))
            unreal.log_warning(f"CalcOceanFoam Input[{idx}]: {inp.get_name()} ({cname}) Val={val}")

