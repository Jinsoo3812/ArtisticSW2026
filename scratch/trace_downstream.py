import unreal

material = unreal.load_asset("/Game/New/Water/Realistic_Water/M_Realistic_Water")
prefix = "/Game/New/Water/Realistic_Water/M_Realistic_Water.M_Realistic_Water:"

def trace_forward(node, depth=0):
    if depth > 10:
        return
    indent = "  " * depth
    nname = node.get_name()
    cname = node.get_class().get_name()
    unreal.log_warning(f"{indent}-> {nname} ({cname})")
    
    # Search all expressions to see who has 'node' as an input
    for i in range(900):
        for class_name in [
            "MaterialExpressionAdd", "MaterialExpressionMultiply",
            "MaterialExpressionSetMaterialAttributes", "MaterialExpressionMaterialFunctionCall",
            "MaterialExpressionAppendVector", "MaterialExpressionComponentMask"
        ]:
            obj = unreal.load_object(None, f"{prefix}{class_name}_{i}")
            if obj and obj != node:
                inputs = list(unreal.MaterialEditingLibrary.get_inputs_for_material_expression(material, obj))
                if node in inputs:
                    trace_forward(obj, depth + 1)

custom = None
for i in range(900):
    node = unreal.load_object(None, f"{prefix}MaterialExpressionCustom_{i}")
    if node and "SW Kelvin Wake" in str(node.get_editor_property("description")):
        custom = node
        break

if custom:
    unreal.log_warning(f"Starting trace from {custom.get_name()}:")
    trace_forward(custom, 0)
