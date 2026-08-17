import unreal

material = unreal.load_asset("/Game/New/Water/Realistic_Water/M_Realistic_Water")
prefix = "/Game/New/Water/Realistic_Water/M_Realistic_Water.M_Realistic_Water:"

def trace_up(node, depth=0, max_depth=5):
    if not node or depth > max_depth: return
    indent = "  " * depth
    nname = node.get_name()
    cname = node.get_class().get_name()
    pname = node.get_editor_property("parameter_name") if hasattr(node, "parameter_name") else ""
    desc = node.get_editor_property("description") if hasattr(node, "description") else ""
    unreal.log_warning(f"{indent}- {nname} ({cname}) Param='{pname}' Desc='{desc}'")
    inps = list(unreal.MaterialEditingLibrary.get_inputs_for_material_expression(material, node))
    for inp in inps:
        if inp:
            trace_up(inp, depth + 1, max_depth)

sub_6 = unreal.load_object(None, f"{prefix}MaterialExpressionSubtract_6")
if sub_6:
    unreal.log_warning("=== Subtract_6 Upstream Trace ===")
    trace_up(sub_6, 0, 6)

mask_10 = unreal.load_object(None, f"{prefix}MaterialExpressionComponentMask_10")
if mask_10:
    unreal.log_warning("=== ComponentMask_10 Upstream Trace ===")
    trace_up(mask_10, 0, 6)
