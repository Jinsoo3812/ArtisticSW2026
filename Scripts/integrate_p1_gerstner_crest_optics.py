"""Integrate P1 Gerstner crest scattering with minimal material graph edits."""
import traceback

import unreal


ASSET_PATH = "/Game/Blueprints/Water/M_Realistic_Water"
OPTICS_DESC = "SW P1 Gerstner Crest Optical Path"
LEGACY_OPTICS_DESC = "SW P1 Gerstner Crest Optical Scattering"
PATH_DESC = "SW P1 Gerstner Crest Optical Path Scale"
WHITE_FOAM_DESC = "SW Gerstner White Foam Only (emerald web disconnected)"
ABSORPTION_MULTIPLY_DESC = "SW P1 Crest Short-Path Absorption"

PATH_CODE = r"""return SW_ComputeGerstnerCrestOpticalPath(
    GerstnerWPO,
    GerstnerNormal,
    CrestBounds,
    ShoulderInfluence,
    MinOpticalPathScale,
    Enabled);"""

OPTICS_CODE = r"""return SW_ApplyGerstnerCrestScattering(
    BaseScattering,
    OpticalPathScale,
    MinOpticalPathScale,
    SubsurfaceTint,
    SubsurfaceTintStrength);"""

WHITE_FOAM_CODE = r"""float H=GerstnerWPO.z;
float S=1.0-saturate(normalize(GerstnerNormal).z);
float crestMask=saturate(
    smoothstep(CrestBounds.r,CrestBounds.g,H)*
    smoothstep(CrestBounds.b,CrestBounds.a,S));
return WhiteFoam*FoamColor*crestMask;"""


def short_name(expression):
    return expression.get_path_name().split(":")[-1]


def find_by_name(expressions, wanted):
    return next((e for e in expressions if short_name(e) == wanted), None)


def find_by_desc(expressions, wanted):
    for expression in expressions:
        for property_name in ("desc", "description"):
            try:
                if str(expression.get_editor_property(property_name)) == wanted:
                    return expression
            except Exception:
                pass
    return None


def find_parameter(expressions, wanted):
    for expression in expressions:
        try:
            if str(expression.get_editor_property("parameter_name")) == wanted:
                return expression
        except Exception:
            pass
    return None


def scalar_parameter(material, editing, expressions, name, default, x, y):
    expression = find_parameter(expressions, name)
    if expression is None:
        expression = editing.create_material_expression(
            material, unreal.MaterialExpressionScalarParameter, x, y)
        expression.set_editor_property("parameter_name", name)
        expressions.append(expression)
    expression.set_editor_property("default_value", default)
    try:
        expression.set_editor_property("group", "P1 Crest Optics")
    except Exception:
        pass
    return expression


def vector_parameter(material, editing, expressions, name, default, x, y):
    expression = find_parameter(expressions, name)
    if expression is None:
        expression = editing.create_material_expression(
            material, unreal.MaterialExpressionVectorParameter, x, y)
        expression.set_editor_property("parameter_name", name)
        expressions.append(expression)
    expression.set_editor_property("default_value", unreal.LinearColor(*default))
    try:
        expression.set_editor_property("group", "P1 Crest Optics")
    except Exception:
        pass
    return expression


def renamed_parameter(expressions, new_name, legacy_name):
    expression = find_parameter(expressions, new_name)
    if expression is None:
        expression = find_parameter(expressions, legacy_name)
        if expression is not None:
            expression.set_editor_property("parameter_name", new_name)
    return expression


def connect(editing, source, output_name, target, input_name):
    if not editing.connect_material_expressions(source, output_name, target, input_name):
        raise RuntimeError(
            "Connection failed: {}.{} -> {}.{}".format(
                short_name(source), output_name, short_name(target), input_name))


def main():
    material = unreal.load_asset(ASSET_PATH)
    if material is None:
        raise RuntimeError("Could not load " + ASSET_PATH)

    helper = unreal.RealisticWaterMaterialPipelineLibrary
    editing = unreal.MaterialEditingLibrary
    expressions = list(helper.get_material_expressions(material))

    gerstner = find_by_name(expressions, "MaterialExpressionMaterialFunctionCall_1")
    coefficient_mask = find_by_name(expressions, "MaterialExpressionMaterialFunctionCall_14")
    water_output = find_by_name(expressions, "MaterialExpressionSingleLayerWaterMaterialOutput_0")
    ocean_foam = find_by_name(expressions, "MaterialExpressionCustom_16")
    white_foam = find_by_name(expressions, "MaterialExpressionCustom_15")
    foam_color = find_by_name(expressions, "MaterialExpressionConstant3Vector_3")
    foam_bounds = find_parameter(expressions, "CrestBounds")

    required = {
        "Gerstner": gerstner,
        "WaterCoefficientMask": coefficient_mask,
        "SingleLayerWaterOutput": water_output,
        "OceanFoam": ocean_foam,
        "WhiteFoam": white_foam,
        "FoamColor": foam_color,
        "CrestBounds": foam_bounds,
    }
    missing = [key for key, value in required.items() if value is None]
    if missing:
        raise RuntimeError("Required graph expressions missing: " + ", ".join(missing))

    # Preserve the white froth sampler and its established height/steepness
    # mask. Rebuilding the Custom input list disconnects the emerald web path
    # without deleting its nodes or assets.
    white_inputs = [
        "WhiteFoam", "FoamColor", "CrestBounds", "GerstnerWPO", "GerstnerNormal"]
    if not helper.configure_float3_custom_expression(
            ocean_foam, white_inputs, WHITE_FOAM_CODE, WHITE_FOAM_DESC):
        raise RuntimeError("Could not configure white-only foam Custom node")
    connect(editing, white_foam, "", ocean_foam, "WhiteFoam")
    connect(editing, foam_color, "", ocean_foam, "FoamColor")
    connect(editing, foam_bounds, "RGBA", ocean_foam, "CrestBounds")
    connect(editing, gerstner, "WPO", ocean_foam, "GerstnerWPO")
    connect(editing, gerstner, "Normal", ocean_foam, "GerstnerNormal")

    bounds = vector_parameter(
        material, editing, expressions, "SW Crest Optics Bounds",
        (30.0, 50.0, 0.0, 0.013), 2600, 1060)
    subsurface_tint = renamed_parameter(
        expressions, "SW Crest Subsurface Tint", "SW Crest Scattering Multiplier")
    if subsurface_tint is None:
        subsurface_tint = vector_parameter(
            material, editing, expressions, "SW Crest Subsurface Tint",
            (0.95, 1.05, 1.10, 1.0), 2600, 1210)
    else:
        subsurface_tint.set_editor_property(
            "default_value", unreal.LinearColor(0.95, 1.05, 1.10, 1.0))
        try:
            subsurface_tint.set_editor_property("group", "P1 Crest Optics")
        except Exception:
            pass
    shoulder = scalar_parameter(
        material, editing, expressions, "SW Crest Shoulder Influence",
        0.20, 2600, 1360)
    min_path_scale = scalar_parameter(
        material, editing, expressions, "SW Crest Min Optical Path Scale",
        0.50, 2600, 1460)
    tint_strength = renamed_parameter(
        expressions, "SW Crest Subsurface Tint Strength", "SW Crest Optics Strength")
    if tint_strength is None:
        tint_strength = scalar_parameter(
            material, editing, expressions, "SW Crest Subsurface Tint Strength",
            0.25, 2600, 1560)
    else:
        tint_strength.set_editor_property("default_value", 0.25)
        try:
            tint_strength.set_editor_property("group", "P1 Crest Optics")
        except Exception:
            pass
    enabled = scalar_parameter(
        material, editing, expressions, "SW Crest Optics Enabled",
        1.0, 2600, 1660)

    optics = find_by_desc(expressions, OPTICS_DESC)
    if optics is None:
        optics = find_by_desc(expressions, LEGACY_OPTICS_DESC)
    if optics is None:
        optics = next((
            expression for expression in expressions
            if isinstance(expression, unreal.MaterialExpressionCustom)
            and "SW_ApplyGerstnerCrestScattering" in str(
                expression.get_editor_property("code"))), None)
    if optics is None:
        optics = editing.create_material_expression(
            material, unreal.MaterialExpressionCustom, 3300, 1180)
    path = find_by_desc(expressions, PATH_DESC)
    if path is None:
        path = editing.create_material_expression(
            material, unreal.MaterialExpressionCustom, 3040, 1180)
    path_inputs = [
        "GerstnerWPO", "GerstnerNormal", "CrestBounds", "ShoulderInfluence",
        "MinOpticalPathScale", "Enabled"]
    if not helper.configure_float1_custom_expression_with_includes(
            path, path_inputs, PATH_CODE, PATH_DESC,
            ["/Project/SWGerstnerCrestOptics.ush"]):
        raise RuntimeError("Could not configure P1 optical path Custom node")
    connect(editing, gerstner, "WPO", path, "GerstnerWPO")
    connect(editing, gerstner, "Normal", path, "GerstnerNormal")
    connect(editing, bounds, "RGBA", path, "CrestBounds")
    connect(editing, shoulder, "", path, "ShoulderInfluence")
    connect(editing, min_path_scale, "", path, "MinOpticalPathScale")
    connect(editing, enabled, "", path, "Enabled")

    optics_inputs = [
        "BaseScattering", "OpticalPathScale", "MinOpticalPathScale",
        "SubsurfaceTint", "SubsurfaceTintStrength"]
    if not helper.configure_float3_custom_expression_with_includes(
            optics, optics_inputs, OPTICS_CODE, OPTICS_DESC,
            ["/Project/SWGerstnerCrestOptics.ush"]):
        raise RuntimeError("Could not configure P1 crest optics Custom node")

    connect(editing, coefficient_mask, "Scattering", optics, "BaseScattering")
    connect(editing, path, "", optics, "OpticalPathScale")
    connect(editing, min_path_scale, "", optics, "MinOpticalPathScale")
    connect(editing, subsurface_tint, "RGB", optics, "SubsurfaceTint")
    connect(editing, tint_strength, "", optics, "SubsurfaceTintStrength")

    absorption_multiply = find_by_desc(expressions, ABSORPTION_MULTIPLY_DESC)
    if absorption_multiply is None:
        absorption_multiply = editing.create_material_expression(
            material, unreal.MaterialExpressionMultiply, 3760, 1510)
    absorption_multiply.set_editor_property("desc", ABSORPTION_MULTIPLY_DESC)

    connect(editing, coefficient_mask, "Absorption", absorption_multiply, "A")
    connect(editing, path, "", absorption_multiply, "B")

    # Scattering and absorption share one crest-derived optical path value.
    # PhaseG and behind-water colour remain untouched.
    connect(editing, optics, "", water_output, "ScatteringCoefficients")
    connect(editing, absorption_multiply, "", water_output, "AbsorptionCoefficients")

    helper.initialize_missing_parameter_guids(material)
    editing.recompile_material(material)
    unreal.EditorAssetLibrary.save_loaded_asset(material, only_if_is_dirty=False)
    unreal.log("SW_P1_SHORT_PATH_OPTICS_INTEGRATED=1")
    unreal.log("SW_P1_EMERALD_WEB_NODE_PRESERVED_AND_DISCONNECTED=1")


try:
    main()
except Exception:
    unreal.log_error(traceback.format_exc())
    raise
