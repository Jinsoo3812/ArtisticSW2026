import unreal

material = unreal.load_asset("/Game/New/Water/Realistic_Water/M_Realistic_Water")
prefix = "/Game/New/Water/Realistic_Water/M_Realistic_Water.M_Realistic_Water:"

# Check Comment_16 and Comment_17 surrounding nodes
com_16 = unreal.load_object(None, f"{prefix}MaterialExpressionComment_16")
com_17 = unreal.load_object(None, f"{prefix}MaterialExpressionComment_17")

def inspect_comment_nodes(com, name):
    if not com: return
    unreal.log_warning(f"=== {name} ===")
    unreal.log_warning(f"Text: {com.get_editor_property('text')}")

inspect_comment_nodes(com_16, "Comment_16")
inspect_comment_nodes(com_17, "Comment_17")

# Let's find what nodes compute WaveSteepness and WaveHeight in the Ocean Foam area
custom_16 = unreal.load_object(None, f"{prefix}MaterialExpressionCustom_16")
inputs = list(unreal.MaterialEditingLibrary.get_inputs_for_material_expression(material, custom_16))

# Input[2] was ComponentMask_9 (WaveHeight)
# Input[3] was OneMinus_0 (WaveSteepness)
unreal.log_warning(f"WaveHeight node: {inputs[2].get_name() if inputs[2] else 'None'}")
unreal.log_warning(f"WaveSteepness node: {inputs[3].get_name() if inputs[3] else 'None'}")

# Trace WaveHeight upstream
def trace_full_chain(node, depth=0):
    if depth > 8 or not node: return
    indent = "  " * depth
    nname = node.get_name()
    cname = node.get_class().get_name()
    pname = node.get_editor_property("parameter_name") if hasattr(node, "parameter_name") else ""
    desc = node.get_editor_property("description") if hasattr(node, "description") else ""
    val = ""
    if "Constant" in cname or "Parameter" in cname:
        if hasattr(node, "default_value"): val = f" DefVal={node.get_editor_property('default_value')}"
        elif hasattr(node, "constant"): val = f" Const={node.get_editor_property('constant')}"
    unreal.log_warning(f"{indent}-> {nname} ({cname}) Param='{pname}' Desc='{desc}'{val}")
    inps = list(unreal.MaterialEditingLibrary.get_inputs_for_material_expression(material, node))
    for inp in inps:
        if inp:
            trace_full_chain(inp, depth + 1)

unreal.log_warning("=== TRACE WAVE HEIGHT ===")
trace_full_chain(inputs[2])

unreal.log_warning("=== TRACE WAVE STEEPNESS ===")
trace_full_chain(inputs[3])

