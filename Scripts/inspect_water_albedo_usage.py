"""Trace Water Albedo inside the engine WaterAttributes material function."""
import traceback
import unreal


def name(obj):
    return obj.get_path_name().split(":")[-1]


def main():
    function = unreal.load_asset("/Water/Materials/Layers/WaterAttributes")
    helper = unreal.RealisticWaterMaterialPipelineLibrary
    expressions = list(helper.get_material_function_expressions(function))
    target = None
    for expression in expressions:
        try:
            if str(expression.get_editor_property("parameter_name")) == "Water Albedo":
                target = expression
                break
        except Exception:
            pass
    if target is None:
        raise RuntimeError("Water Albedo parameter not found")
    unreal.log("WATER_ALBEDO_NODE=" + name(target))
    frontier = [target]
    seen = {target}
    for depth in range(8):
        next_frontier = []
        for candidate in expressions:
            if candidate in seen:
                continue
            for index in range(32):
                if helper.get_connected_input_expression(candidate, index) in frontier:
                    unreal.log("WATER_ALBEDO_EDGE depth={} {} input={}".format(
                        depth + 1, name(candidate), index))
                    for prop in ("parameter_name", "desc"):
                        try:
                            unreal.log("  {}={}".format(
                                prop, candidate.get_editor_property(prop)))
                        except Exception:
                            pass
                    unreal.log("  class=" + candidate.get_class().get_name())
                    unreal.log("  outputs=" + ",".join(
                        str(value) for value in
                        helper.get_material_expression_output_names(candidate)))
                    for input_index in range(4):
                        source = helper.get_connected_input_expression(candidate, input_index)
                        if source:
                            unreal.log("  input{}={}".format(input_index, name(source)))
                    for prop in ("a", "b", "const_a", "const_b", "attribute_set_types"):
                        try:
                            unreal.log("  {}={}".format(
                                prop, candidate.get_editor_property(prop)))
                        except Exception:
                            pass
                    seen.add(candidate)
                    next_frontier.append(candidate)
                    break
        frontier = next_frontier
        if not frontier:
            break


try:
    main()
except Exception:
    unreal.log_error(traceback.format_exc())
    raise
