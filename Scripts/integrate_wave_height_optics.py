"""Replace the former bound-based crest optics with height endpoint interpolation."""
import traceback

import unreal


MASTER_PATH = "/Game/Blueprints/Water/M_Realistic_Water"
CUSTOM_DESC = "SW Wave Height Optical Endpoints"
PARAM_GROUP = "Wave Height Optics"

OLD_PARAMETER_NAMES = {
    "SW Crest Optics Bounds",
    "SW Crest Subsurface Tint",
    "SW Crest Shoulder Influence",
    "SW Crest Min Optical Path Scale",
    "SW Crest Subsurface Tint Strength",
    "SW Crest Optics Enabled",
    "SW Crest Scattering Multiplier",
    "SW Crest Optics Strength",
}
OLD_CODE_TOKENS = {
    "SW_ComputeGerstnerCrestOpticalPath",
    "SW_ApplyGerstnerCrestScattering",
}
OLD_DESCRIPTIONS = {
    "SW P1 Gerstner Crest Optical Path",
    "SW P1 Gerstner Crest Optical Scattering",
    "SW P1 Gerstner Crest Optical Path Scale",
    "SW P1 Crest Short-Path Absorption",
}

CUSTOM_CODE = r"""return SW_InterpolateWaveHeightOptics(
    GerstnerWPO,
    GerstnerNormal,
    CameraVector,
    HeightRange,
    TroughScattering,
    CrestScattering,
    TroughAbsorption,
    CrestAbsorption,
    Enabled,
    ScatteringA,
    AbsorptionRGB,
    AbsorptionA);"""


def short_name(expression):
    return expression.get_path_name().split(":")[-1] if expression else "None"


def get_property(expression, name, default=""):
    try:
        return expression.get_editor_property(name)
    except Exception:
        return default


def find_by_name(expressions, wanted):
    return next((e for e in expressions if short_name(e) == wanted), None)


def find_parameter(expressions, wanted):
    return next((e for e in expressions
                 if str(get_property(e, "parameter_name")) == wanted), None)


def find_by_desc(expressions, wanted):
    for expression in expressions:
        if (str(get_property(expression, "description")) == wanted
                or str(get_property(expression, "desc")) == wanted):
            return expression
    return None


def connect(editing, source, output_name, target, input_name):
    if not editing.connect_material_expressions(source, output_name, target, input_name):
        raise RuntimeError("Connection failed: {}.{} -> {}.{}".format(
            short_name(source), output_name, short_name(target), input_name))


def vector_parameter(material, editing, expressions, name, default, x, y):
    expression = find_parameter(expressions, name)
    if expression is None:
        expression = editing.create_material_expression(
            material, unreal.MaterialExpressionVectorParameter, x, y)
        expression.set_editor_property("parameter_name", name)
        expressions.append(expression)
    expression.set_editor_property("default_value", unreal.LinearColor(*default))
    expression.set_editor_property("group", PARAM_GROUP)
    return expression


def scalar_parameter(material, editing, expressions, name, default, x, y):
    expression = find_parameter(expressions, name)
    if expression is None:
        expression = editing.create_material_expression(
            material, unreal.MaterialExpressionScalarParameter, x, y)
        expression.set_editor_property("parameter_name", name)
        expressions.append(expression)
    expression.set_editor_property("default_value", default)
    expression.set_editor_property("group", PARAM_GROUP)
    return expression


def is_old_p1_expression(expression):
    parameter_name = str(get_property(expression, "parameter_name"))
    if parameter_name in OLD_PARAMETER_NAMES:
        return True
    description = str(get_property(expression, "desc", get_property(expression, "description")))
    if description in OLD_DESCRIPTIONS:
        return True
    if isinstance(expression, unreal.MaterialExpressionCustom):
        code = str(get_property(expression, "code"))
        return any(token in code for token in OLD_CODE_TOKENS)
    return False


def main():
    master = unreal.load_asset(MASTER_PATH)
    if master is None:
        raise RuntimeError("Could not load master water material")

    helper = unreal.RealisticWaterMaterialPipelineLibrary
    editing = unreal.MaterialEditingLibrary
    expressions = list(helper.get_material_expressions(master))

    gerstner = find_by_name(expressions, "MaterialExpressionMaterialFunctionCall_1")
    scattering = find_parameter(expressions, "Scattering")
    absorption = find_parameter(expressions, "Absorption")
    scattering_multiply = find_by_name(expressions, "MaterialExpressionMultiply_0")
    absorption_reciprocal = find_by_name(expressions, "MaterialExpressionDivide_2")
    absorption_scale = find_by_name(expressions, "MaterialExpressionDivide_1")
    coefficient_mask = find_by_name(expressions, "MaterialExpressionMaterialFunctionCall_14")
    water_output = find_by_name(expressions, "MaterialExpressionSingleLayerWaterMaterialOutput_0")
    camera_vector = next((e for e in expressions
                          if isinstance(e, unreal.MaterialExpressionCameraVectorWS)), None)
    if camera_vector is None:
        camera_vector = editing.create_material_expression(
            master, unreal.MaterialExpressionCameraVectorWS, 820, 1840)
        expressions.append(camera_vector)

    required = {
        "Gerstner": gerstner,
        "Scattering": scattering,
        "Absorption": absorption,
        "ScatteringMultiply": scattering_multiply,
        "AbsorptionReciprocal": absorption_reciprocal,
        "AbsorptionScale": absorption_scale,
        "WaterCoefficientMask": coefficient_mask,
        "SingleLayerWaterOutput": water_output,
    }
    missing = [name for name, value in required.items() if value is None]
    if missing:
        raise RuntimeError("Required graph expressions missing: " + ", ".join(missing))

    # Reuse the old scattering Custom when possible so the graph gains only one
    # runtime node. Its former inputs and additional outputs are fully rebuilt.
    connected_scattering = helper.get_connected_input_expression(scattering_multiply, 0)
    custom = (connected_scattering
              if isinstance(connected_scattering, unreal.MaterialExpressionCustom)
              and "SW_InterpolateWaveHeightOptics" in str(
                  get_property(connected_scattering, "code")) else None)
    if custom is None:
        custom = find_by_desc(expressions, CUSTOM_DESC)
    if custom is None:
        custom = next((e for e in expressions
                       if isinstance(e, unreal.MaterialExpressionCustom)
                       and "SW_ApplyGerstnerCrestScattering" in str(get_property(e, "code"))), None)
    if custom is None:
        custom = editing.create_material_expression(
            master, unreal.MaterialExpressionCustom, 1080, 1940)
        expressions.append(custom)

    crest_scattering = vector_parameter(
        master, editing, expressions, "SW Crest Scattering",
        (1.0, 1.0, 1.0, 0.5), 560, 1960)
    crest_absorption = vector_parameter(
        master, editing, expressions, "SW Crest Absorption",
        (10.0, 150.0, 350.0, 8.0), 560, 2120)
    height_range = vector_parameter(
        master, editing, expressions, "SW Wave Optics Height Range",
        (-100.0, 100.0, 0.0, 0.0), 560, 2280)
    enabled = scalar_parameter(
        master, editing, expressions, "SW Wave Optics Enabled",
        1.0, 560, 2440)

    input_names = [
        "GerstnerWPO", "GerstnerNormal", "CameraVector", "HeightRange",
        "TroughScattering", "CrestScattering", "TroughAbsorption",
        "CrestAbsorption", "Enabled"]
    if not helper.configure_wave_height_optics_custom_expression(
            custom, input_names, CUSTOM_CODE, CUSTOM_DESC,
            ["/Project/SWWaveHeightOptics.ush"]):
        raise RuntimeError("Could not configure wave-height optics Custom")

    connect(editing, gerstner, "WPO", custom, "GerstnerWPO")
    connect(editing, gerstner, "Normal", custom, "GerstnerNormal")
    connect(editing, camera_vector, "", custom, "CameraVector")
    connect(editing, height_range, "RGBA", custom, "HeightRange")
    connect(editing, scattering, "RGBA", custom, "TroughScattering")
    connect(editing, crest_scattering, "RGBA", custom, "CrestScattering")
    connect(editing, absorption, "RGBA", custom, "TroughAbsorption")
    connect(editing, crest_absorption, "RGBA", custom, "CrestAbsorption")
    connect(editing, enabled, "", custom, "Enabled")

    # Keep the engine's established coefficient conversions intact:
    # Scattering = RGB*A/1000, Absorption = 1/(RGB*A).
    connect(editing, custom, "", scattering_multiply, "A")
    connect(editing, custom, "ScatteringA", scattering_multiply, "B")
    connect(editing, custom, "AbsorptionRGB", absorption_reciprocal, "B")
    connect(editing, absorption_reciprocal, "", absorption_scale, "A")
    connect(editing, custom, "AbsorptionA", absorption_scale, "B")

    # Bypass and remove all former P1 post-processing. Water_Underside and
    # WaterCoefficientMask remain the authoritative engine pipeline.
    connect(editing, coefficient_mask, "Scattering", water_output, "ScatteringCoefficients")
    connect(editing, coefficient_mask, "Absorption", water_output, "AbsorptionCoefficients")

    removed = 0
    for expression in list(expressions):
        if expression != custom and is_old_p1_expression(expression):
            editing.delete_material_expression(master, expression)
            removed += 1
        elif (expression != custom
              and isinstance(expression, unreal.MaterialExpressionCustom)
              and "SW_InterpolateWaveHeightOptics" in str(get_property(expression, "code"))):
            editing.delete_material_expression(master, expression)
            removed += 1

    helper.initialize_missing_parameter_guids(master)
    editing.recompile_material(master)
    unreal.EditorAssetLibrary.save_loaded_asset(master, only_if_is_dirty=False)

    unreal.log("SW_WAVE_HEIGHT_OPTICS_INTEGRATED=1")
    unreal.log("SW_WAVE_HEIGHT_OPTICS_OLD_P1_REMOVED={}".format(removed))
    unreal.log("SW_WAVE_HEIGHT_OPTICS_MI_OVERRIDES_PRESERVED=1")


try:
    main()
except Exception:
    unreal.log_error(traceback.format_exc())
    raise
