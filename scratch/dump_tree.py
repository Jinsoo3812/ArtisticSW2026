import unreal

material = unreal.load_asset("/Game/New/Water/Realistic_Water/M_Realistic_Water")
prefix = "/Game/New/Water/Realistic_Water/M_Realistic_Water.M_Realistic_Water:"

def dump_node_tree(node, depth=0, max_depth=4):
    if not node or depth > max_depth:
        return
    indent = "  " * depth
    nname = node.get_name()
    cname = node.get_class().get_name()
    
    desc_str = ""
    if "Custom" in cname:
        desc_str = f" [Desc: {node.get_editor_property('description')}]"
    elif "StaticSwitchParameter" in cname or "ScalarParameter" in cname or "VectorParameter" in cname:
        desc_str = f" [Param: {node.get_editor_property('parameter_name')}]"
    elif "MaterialFunctionCall" in cname:
        fn = node.get_editor_property("material_function")
        desc_str = f" [Func: {fn.get_name() if fn else 'None'}]"
        
    unreal.log_warning(f"{indent}- {nname} ({cname}){desc_str}")
    
    inputs = list(unreal.MaterialEditingLibrary.get_inputs_for_material_expression(material, node))
    for idx, inp in enumerate(inputs):
        if inp:
            dump_node_tree(inp, depth + 1, max_depth)

# Check SetMaterialAttributes_2
set_mat_2 = unreal.load_object(None, f"{prefix}MaterialExpressionSetMaterialAttributes_2")
if set_mat_2:
    unreal.log_warning("=== TREE FOR SetMaterialAttributes_2 ===")
    dump_node_tree(set_mat_2, 0, 5)

# Check SetMaterialAttributes_0
set_mat_0 = unreal.load_object(None, f"{prefix}MaterialExpressionSetMaterialAttributes_0")
if set_mat_0:
    unreal.log_warning("=== TREE FOR SetMaterialAttributes_0 ===")
    dump_node_tree(set_mat_0, 0, 5)

# Check Material Root Inputs
unreal.log_warning("=== MATERIAL ROOT INPUTS ===")
for i in range(900):
    for cname in ["MaterialExpressionSetMaterialAttributes"]:
        obj = unreal.load_object(None, f"{prefix}{cname}_{i}")
        if obj:
            unreal.log_warning(f"Found SetMaterialAttributes: {obj.get_name()}")
