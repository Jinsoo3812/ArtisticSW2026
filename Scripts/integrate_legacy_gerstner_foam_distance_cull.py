"""Add camera-distance culling only to the active legacy emissive Gerstner foam."""
import traceback

import unreal


MATERIAL_PATH = "/Game/Blueprints/Water/M_Realistic_Water"
LEGACY_NODE_NAME = "MaterialExpressionCustom_16"
LEGACY_DESC = "Legacy emissive Gerstner foam (disconnected; preserved)"
GROUP = "Legacy Gerstner Foam"
PARAMETER_NAME = "Legacy Gerstner Foam Cull Distance"

LEGACY_CODE = r"""float H=GerstnerWPO.z;
float S=1.0-saturate(normalize(GerstnerNormal).z);
float crestMask=saturate(
    smoothstep(CrestBounds.r,CrestBounds.g,H)*
    smoothstep(CrestBounds.b,CrestBounds.a,S));
float3 LegacyFoam=WhiteFoam*FoamColor*crestMask;
return SW_ApplyLegacyGerstnerFoamDistanceCull(
    LegacyFoam,
    CameraDistance,
    FoamCullDistance);"""


def short_name(expression):
    return expression.get_path_name().split(":")[-1] if expression else "None"


def prop(expression, name, default=None):
    try:
        return expression.get_editor_property(name)
    except Exception:
        return default


def find_parameter(expressions, wanted):
    return next((expression for expression in expressions
                 if str(prop(expression, "parameter_name", "")) == wanted), None)


def find_desc(expressions, wanted):
    return next((expression for expression in expressions
                 if str(prop(expression, "desc", "")) == wanted), None)


def connect(editing, source, output_name, target, input_name):
    if not editing.connect_material_expressions(
            source, output_name, target, input_name):
        raise RuntimeError("Connection failed: {}.{} -> {}.{}".format(
            short_name(source), output_name, short_name(target), input_name))


def main():
    material = unreal.load_asset(MATERIAL_PATH)
    if material is None:
        raise RuntimeError("Could not load " + MATERIAL_PATH)

    helper = unreal.RealisticWaterMaterialPipelineLibrary
    editing = unreal.MaterialEditingLibrary
    expressions = list(helper.get_material_expressions(material))
    legacy = next((expression for expression in expressions
                   if short_name(expression) == LEGACY_NODE_NAME), None)
    if not isinstance(legacy, unreal.MaterialExpressionCustom):
        raise RuntimeError("Legacy emissive Gerstner Custom_16 is missing")

    original_inputs = [
        helper.get_connected_input_expression(legacy, index)
        for index in range(5)
    ]
    if not all(original_inputs):
        raise RuntimeError("Legacy Gerstner foam inputs are disconnected")

    cull_distance = find_parameter(expressions, PARAMETER_NAME)
    if cull_distance is None:
        cull_distance = editing.create_material_expression(
            material, unreal.MaterialExpressionScalarParameter, 2960, 3710)
        cull_distance.set_editor_property("parameter_name", PARAMETER_NAME)
        expressions.append(cull_distance)
    cull_distance.set_editor_property("default_value", 50000.0)
    cull_distance.set_editor_property("group", GROUP)

    camera_position = find_desc(expressions, "Legacy Foam Camera Position")
    if camera_position is None:
        camera_position = editing.create_material_expression(
            material, unreal.MaterialExpressionCameraPositionWS, 2960, 3830)
        camera_position.set_editor_property("desc", "Legacy Foam Camera Position")
        expressions.append(camera_position)

    world_position = find_desc(expressions, "Legacy Foam World Position")
    if world_position is None:
        world_position = editing.create_material_expression(
            material, unreal.MaterialExpressionWorldPosition, 2960, 3950)
        world_position.set_editor_property("desc", "Legacy Foam World Position")
        try:
            world_position.set_editor_property(
                "world_position_shader_offset",
                unreal.WorldPositionIncludedOffsets.WPT_EXCLUDE_ALL_SHADER_OFFSETS)
        except Exception:
            pass
        expressions.append(world_position)

    camera_distance = find_desc(expressions, "Legacy Foam Camera Distance")
    if camera_distance is None:
        camera_distance = editing.create_material_expression(
            material, unreal.MaterialExpressionDistance, 3280, 3830)
        camera_distance.set_editor_property("desc", "Legacy Foam Camera Distance")
        expressions.append(camera_distance)
    connect(editing, camera_position, "", camera_distance, "A")
    connect(editing, world_position, "", camera_distance, "B")

    input_names = [
        "WhiteFoam", "FoamColor", "CrestBounds", "GerstnerWPO",
        "GerstnerNormal", "CameraDistance", "FoamCullDistance"]
    if not helper.configure_float3_custom_expression_with_includes(
            legacy, input_names, LEGACY_CODE, LEGACY_DESC,
            ["/Project/SWFluxOceanFoam.ush"]):
        raise RuntimeError("Could not configure legacy foam Custom")

    connect(editing, original_inputs[0], "", legacy, "WhiteFoam")
    connect(editing, original_inputs[1], "", legacy, "FoamColor")
    connect(editing, original_inputs[2], "RGBA", legacy, "CrestBounds")
    connect(editing, original_inputs[3], "WPO", legacy, "GerstnerWPO")
    connect(editing, original_inputs[4], "Normal", legacy, "GerstnerNormal")
    connect(editing, camera_distance, "", legacy, "CameraDistance")
    connect(editing, cull_distance, "", legacy, "FoamCullDistance")

    helper.initialize_missing_parameter_guids(material)
    editing.recompile_material(material)
    unreal.EditorAssetLibrary.save_loaded_asset(material, only_if_is_dirty=False)
    unreal.log("LEGACY_GERSTNER_FOAM_DISTANCE_CULL_INTEGRATED=1")


try:
    main()
except Exception:
    unreal.log_error(traceback.format_exc())
    raise
