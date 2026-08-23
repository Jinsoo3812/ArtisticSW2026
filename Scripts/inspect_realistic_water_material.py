import json
import os
import traceback

import unreal


ASSET_PATH = "/Game/Blueprints/Water/M_Realistic_Water"
OUTPUT_PATH = os.path.join(
    unreal.Paths.project_saved_dir(), "Diagnostics", "M_Realistic_Water_Graph.json"
)


def safe_get(obj, name):
    try:
        value = obj.get_editor_property(name)
        if isinstance(value, unreal.Object):
            return value.get_path_name()
        if isinstance(value, (str, int, float, bool)) or value is None:
            return value
        if isinstance(value, (list, tuple)):
            return [serialize_value(item) for item in value]
        return str(value)
    except Exception:
        return None


def serialize_value(value):
    if isinstance(value, unreal.Object):
        return value.get_path_name()
    if isinstance(value, (str, int, float, bool)) or value is None:
        return value
    return str(value)


def expression_name(expr):
    return expr.get_path_name().split(":")[-1]


def inspect_expression(expr, helper):
    record = {
        "name": expression_name(expr),
        "path": expr.get_path_name(),
        "class": expr.get_class().get_name(),
        "outputs": [str(name) for name in helper.get_material_expression_output_names(expr)],
    }
    properties = [
        "desc", "material_expression_editor_x", "material_expression_editor_y",
        "parameter_name", "default_value", "texture", "sampler_type",
        "code", "output_type", "include_file_paths", "additional_outputs",
        "function", "material_function", "attribute_set_types", "const_a", "const_b",
        "constant", "period", "speed", "coordinate_index", "channel_names",
    ]
    values = {}
    for prop in properties:
        value = safe_get(expr, prop)
        if value is not None and value != "":
            values[prop] = value
    record["properties"] = values

    inputs = []
    empty_run = 0
    for input_index in range(64):
        try:
            upstream = helper.get_connected_input_expression(expr, input_index)
        except Exception:
            upstream = None
        if upstream:
            inputs.append({
                "input_index": input_index,
                "from": expression_name(upstream),
                "from_class": upstream.get_class().get_name(),
            })
            empty_run = 0
        else:
            empty_run += 1
            if input_index >= 16 and empty_run >= 16:
                break
    record["connected_inputs"] = inputs
    return record


def main():
    material = unreal.load_asset(ASSET_PATH)
    if not material:
        raise RuntimeError("Could not load " + ASSET_PATH)

    helper = unreal.RealisticWaterMaterialPipelineLibrary
    expressions = list(helper.get_material_expressions(material))
    result = {
        "asset": material.get_path_name(),
        "class": material.get_class().get_name(),
        "expression_count": len(expressions),
        "material_properties": {},
        "expressions": [],
        "material_property_inputs": {},
    }

    for prop in [
        "material_domain", "blend_mode", "shading_model", "two_sided",
        "use_material_attributes", "allow_negative_emissive_color",
        "translucency_lighting_mode", "d3d11_tessellation_mode",
    ]:
        value = safe_get(material, prop)
        if value is not None:
            result["material_properties"][prop] = value

    result["expressions"] = [inspect_expression(expr, helper) for expr in expressions]

    editing = unreal.MaterialEditingLibrary
    property_names = [
        "MP_BASE_COLOR", "MP_METALLIC", "MP_SPECULAR", "MP_ROUGHNESS", "MP_EMISSIVE_COLOR",
        "MP_OPACITY", "MP_OPACITY_MASK", "MP_NORMAL", "MP_WORLD_POSITION_OFFSET",
        "MP_SUBSURFACE_COLOR", "MP_AMBIENT_OCCLUSION", "MP_REFRACTION",
        "MP_MATERIAL_ATTRIBUTES", "MP_PIXEL_DEPTH_OFFSET",
    ]
    for property_name in property_names:
        enum_value = getattr(unreal.MaterialProperty, property_name, None)
        if enum_value is None:
            continue
        try:
            node = editing.get_material_property_input_node(material, enum_value)
            result["material_property_inputs"][property_name] = (
                expression_name(node) if node else None
            )
        except Exception as exc:
            result["material_property_inputs"][property_name] = "ERROR: " + str(exc)

    os.makedirs(os.path.dirname(OUTPUT_PATH), exist_ok=True)
    with open(OUTPUT_PATH, "w", encoding="utf-8") as stream:
        json.dump(result, stream, ensure_ascii=False, indent=2)
    unreal.log("WATER_MATERIAL_INSPECTION=" + OUTPUT_PATH)
    unreal.log("WATER_MATERIAL_EXPRESSIONS=" + str(len(expressions)))


try:
    main()
except Exception:
    unreal.log_error(traceback.format_exc())
    raise
