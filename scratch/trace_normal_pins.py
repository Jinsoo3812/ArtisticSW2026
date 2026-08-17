import unreal

material = unreal.load_asset("/Game/New/Water/Realistic_Water/M_Realistic_Water")
prefix = "/Game/New/Water/Realistic_Water/M_Realistic_Water.M_Realistic_Water:"

set_mat_2 = unreal.load_object(None, f"{prefix}MaterialExpressionSetMaterialAttributes_2")
inputs = list(unreal.MaterialEditingLibrary.get_inputs_for_material_expression(material, set_mat_2))

unreal.log_warning("=== SetMaterialAttributes_2 Inputs Detailed ===")
for idx, inp in enumerate(inputs):
    if inp:
        nname = inp.get_name()
        cname = inp.get_class().get_name()
        desc = ""
        if "Custom" in cname:
            desc = f"Desc='{inp.get_editor_property('description')}'"
        elif "Parameter" in cname:
            desc = f"Param='{inp.get_editor_property('parameter_name')}'"
        unreal.log_warning(f"  Input[{idx}]: {nname} ({cname}) {desc}")
    else:
        unreal.log_warning(f"  Input[{idx}]: None")

# Where does Custom_5 (Calc_Normal_Godot) go?
custom_5 = unreal.load_object(None, f"{prefix}MaterialExpressionCustom_5")
unreal.log_warning("=== Custom_5 Downstream Trace ===")
def trace_out(node, depth=0):
    if depth > 6 or not node: return
    indent = "  " * depth
    unreal.log_warning(f"{indent}-> {node.get_name()} ({node.get_class().get_name()})")
    for i in range(500):
        for cn in ["MaterialExpressionSetMaterialAttributes", "MaterialExpressionMaterialFunctionCall", "MaterialExpressionAdd", "MaterialExpressionMultiply", "MaterialExpressionLinearInterpolate", "MaterialExpressionReroute", "MaterialExpressionStaticSwitchParameter"]:
            obj = unreal.load_object(None, f"{prefix}{cn}_{i}")
            if obj and obj != node:
                inps = list(unreal.MaterialEditingLibrary.get_inputs_for_material_expression(material, obj))
                if node in inps:
                    trace_out(obj, depth + 1)

trace_out(custom_5, 0)
