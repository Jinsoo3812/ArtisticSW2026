import unreal

material = unreal.load_asset("/Game/New/Water/Realistic_Water/M_Realistic_Water")
prefix = "/Game/New/Water/Realistic_Water/M_Realistic_Water.M_Realistic_Water:"

custom_16 = unreal.load_object(None, f"{prefix}MaterialExpressionCustom_16")
inputs = list(unreal.MaterialEditingLibrary.get_inputs_for_material_expression(material, custom_16))

unreal.log_warning("=== Bounds Parameters for CalcOceanFoam ===")
# Input[4]: Constant3Vector_3 (SlopeBounds)
# Input[5]: Constant3Vector_1 (CrestBounds)
for idx in [4, 5]:
    inp = inputs[idx]
    if inp:
        cname = inp.get_class().get_name()
        val = "N/A"
        if hasattr(inp, "constant"): val = str(inp.get_editor_property("constant"))
        elif hasattr(inp, "default_value"): val = str(inp.get_editor_property("default_value"))
        unreal.log_warning(f"  Input[{idx}]: {inp.get_name()} ({cname}) -> Value = {val}")

# Also check Water Mesh Actor & Water Body Ocean properties in Realistic_Water map!
map_path = "/Game/New/Water/Realistic_Water/Realistic_Water"
world = unreal.load_asset(map_path)
# Let's inspect WaterMesh properties if any
