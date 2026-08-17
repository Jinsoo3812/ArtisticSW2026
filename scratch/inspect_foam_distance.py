import unreal

material = unreal.load_asset("/Game/New/Water/Realistic_Water/M_Realistic_Water")
prefix = "/Game/New/Water/Realistic_Water/M_Realistic_Water.M_Realistic_Water:"

unreal.log_warning("=== Inspecting Foam & Distance Fade Logic ===")

# 1. Custom_15, Custom_16, Custom_17 (Foam functions)
for cn_id in [15, 16, 17, 1, 2, 3]:
    cn = unreal.load_object(None, f"{prefix}MaterialExpressionCustom_{cn_id}")
    if cn:
        desc = cn.get_editor_property("description")
        code = str(cn.get_editor_property("code"))
        inputs = list(unreal.MaterialEditingLibrary.get_inputs_for_material_expression(material, cn))
        input_names = [x.get_name() if x else "None" for x in inputs]
        unreal.log_warning(f"--- Custom_{cn_id} ({desc}) ---")
        unreal.log_warning(f"Inputs: {input_names}")
        unreal.log_warning(f"Code:\n{code}\n")

# 2. Check all scalar parameters related to Foam, Distance, Fade, LOD, Bounds
for i in range(500):
    sp = unreal.load_object(None, f"{prefix}MaterialExpressionScalarParameter_{i}")
    if sp:
        pname = str(sp.get_editor_property("parameter_name"))
        dval = sp.get_editor_property("default_value")
        if any(k in pname.lower() for k in ["foam", "fade", "dist", "depth", "bound", "range", "far", "bias", "slope", "crest", "lod"]):
            unreal.log_warning(f"ScalarParam: '{pname}' = {dval}")

# 3. Check vector parameters related to Foam
for i in range(500):
    vp = unreal.load_object(None, f"{prefix}MaterialExpressionVectorParameter_{i}")
    if vp:
        pname = str(vp.get_editor_property("parameter_name"))
        dval = vp.get_editor_property("default_value")
        if any(k in pname.lower() for k in ["foam", "fade", "dist", "bound", "slope", "crest"]):
            unreal.log_warning(f"VectorParam: '{pname}' = {dval}")

