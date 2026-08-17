import unreal

material = unreal.load_asset("/Game/New/Water/Realistic_Water/M_Realistic_Water")

prefix = "/Game/New/Water/Realistic_Water/M_Realistic_Water.M_Realistic_Water:"
custom = None
for i in range(900):
    node = unreal.load_object(None, f"{prefix}MaterialExpressionCustom_{i}")
    if node and "SW Kelvin Wake" in str(node.get_editor_property("description")):
        custom = node
        break

if custom:
    unreal.log_warning(f"Found Custom node: {custom.get_name()}")
    code = str(custom.get_editor_property("code"))
    unreal.log_warning(f"Code:\n{code}")
    
    # Check what uses this custom node output
    # Find all expressions connected to custom
    # Look for Add nodes or SetMaterialAttributes
    expressions = []
    for i in range(900):
        for class_name in ["MaterialExpressionAdd", "MaterialExpressionMultiply", "MaterialExpressionSetMaterialAttributes"]:
            obj = unreal.load_object(None, f"{prefix}{class_name}_{i}")
            if obj:
                inputs = list(unreal.MaterialEditingLibrary.get_inputs_for_material_expression(material, obj))
                if custom in inputs:
                    unreal.log_warning(f"Custom node is connected to {obj.get_name()} ({class_name})")
else:
    unreal.log_error("Custom node NOT found in M_Realistic_Water!")
