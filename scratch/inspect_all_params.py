import unreal

material = unreal.load_asset("/Game/New/Water/Realistic_Water/M_Realistic_Water")
prefix = "/Game/New/Water/Realistic_Water/M_Realistic_Water.M_Realistic_Water:"

unreal.log_warning("=== Checking All Parameters in M_Realistic_Water ===")
for i in range(500):
    sp = unreal.load_object(None, f"{prefix}MaterialExpressionScalarParameter_{i}")
    if sp:
        pname = str(sp.get_editor_property("parameter_name"))
        val = sp.get_editor_property("default_value")
        unreal.log_warning(f"ScalarParam[{i}]: '{pname}' = {val}")

for i in range(500):
    vp = unreal.load_object(None, f"{prefix}MaterialExpressionVectorParameter_{i}")
    if vp:
        pname = str(vp.get_editor_property("parameter_name"))
        val = str(vp.get_editor_property("default_value"))
        unreal.log_warning(f"VectorParam[{i}]: '{pname}' = {val}")

