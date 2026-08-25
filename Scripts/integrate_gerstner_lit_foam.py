"""Replace the white Jacobian preview with textured, lit SLW foam."""
import traceback

import unreal


MASTER_PATH = "/Game/Blueprints/Water/M_Realistic_Water"
GROUP = "Gerstner Compression Foam"
SURFACE_DESC = "SW Gerstner Compression Lit Foam Surface"
ZERO_DESC = "SW Gerstner Foam Emissive Disabled"
LEGACY_DESC = "Legacy emissive Gerstner foam (disconnected; preserved)"

CUSTOM_CODE = r"""return SW_ComposeGerstnerLitFoam(
    Compression,
    FoamTexture,
    WaterBaseColor,
    WaterOpacity,
    WaterRoughness,
    Enabled,
    Intensity,
    TexturePower,
    FoamColor,
    OpacityTarget,
    RoughnessTarget,
    FoamOpacity,
    FoamRoughness);"""


def short_name(expression):
    return expression.get_path_name().split(":")[-1] if expression else "None"


def prop(expression, name, default=""):
    try:
        return expression.get_editor_property(name)
    except Exception:
        return default


def find_by_name(expressions, wanted):
    return next((e for e in expressions if short_name(e) == wanted), None)


def find_by_desc(expressions, wanted):
    return next((e for e in expressions
                 if str(prop(e, "description", prop(e, "desc"))) == wanted), None)


def find_parameter(expressions, wanted):
    return next((e for e in expressions
                 if str(prop(e, "parameter_name")) == wanted), None)


def connect(editing, source, output_name, target, input_name):
    if not editing.connect_material_expressions(source, output_name, target, input_name):
        raise RuntimeError("Connection failed: {}.{} -> {}.{}".format(
            short_name(source), output_name, short_name(target), input_name))


def scalar_parameter(material, editing, expressions, name, default, x, y):
    expression = find_parameter(expressions, name)
    if expression is None:
        expression = editing.create_material_expression(
            material, unreal.MaterialExpressionScalarParameter, x, y)
        expression.set_editor_property("parameter_name", name)
        expressions.append(expression)
    expression.set_editor_property("default_value", default)
    expression.set_editor_property("group", GROUP)
    return expression


def vector_parameter(material, editing, expressions, name, default, x, y):
    expression = find_parameter(expressions, name)
    if expression is None:
        expression = editing.create_material_expression(
            material, unreal.MaterialExpressionVectorParameter, x, y)
        expression.set_editor_property("parameter_name", name)
        expressions.append(expression)
    expression.set_editor_property("default_value", unreal.LinearColor(*default))
    expression.set_editor_property("group", GROUP)
    return expression


def main():
    material = unreal.load_asset(MASTER_PATH)
    if material is None:
        raise RuntimeError("Could not load " + MASTER_PATH)

    helper = unreal.RealisticWaterMaterialPipelineLibrary
    editing = unreal.MaterialEditingLibrary
    expressions = list(helper.get_material_expressions(material))

    final_attributes = find_by_name(expressions, "MaterialExpressionSetMaterialAttributes_2")
    compression = find_by_desc(expressions, "SW Gerstner Crest Compression Mask")
    foam_texture = find_by_name(expressions, "MaterialExpressionCustom_15")
    water_base_color = find_by_name(expressions, "MaterialExpressionLinearInterpolate_0")
    water_attributes = find_by_name(expressions, "MaterialExpressionBreakMaterialAttributes_1")
    legacy_foam = find_by_name(expressions, "MaterialExpressionCustom_16")
    required = {
        "FinalAttributes": final_attributes,
        "Compression": compression,
        "FrothTextureEvaluator": foam_texture,
        "WaterBaseColor": water_base_color,
        "WaterAttributes": water_attributes,
        "LegacyFoam": legacy_foam,
    }
    missing = [name for name, value in required.items() if value is None]
    if missing:
        raise RuntimeError("Required expressions missing: " + ", ".join(missing))

    parameters = {
        "Enabled": scalar_parameter(
            material, editing, expressions, "SW Gerstner Foam Enabled", 1.0, 7800, 4740),
        "Intensity": scalar_parameter(
            material, editing, expressions, "SW Gerstner Foam Intensity", 1.0, 7800, 4860),
        "TexturePower": scalar_parameter(
            material, editing, expressions, "SW Gerstner Foam Texture Power", 1.0, 7800, 4980),
        "OpacityTarget": scalar_parameter(
            material, editing, expressions, "SW Gerstner Foam Opacity", 0.85, 7800, 5100),
        "RoughnessTarget": scalar_parameter(
            material, editing, expressions, "SW Gerstner Foam Roughness", 0.80, 7800, 5220),
        "FoamColor": vector_parameter(
            material, editing, expressions, "SW Gerstner Foam Color",
            (0.95, 0.98, 1.0, 1.0), 7800, 5340),
    }

    surface = find_by_desc(expressions, SURFACE_DESC)
    if surface is None:
        surface = editing.create_material_expression(
            material, unreal.MaterialExpressionCustom, 8460, 4740)
        expressions.append(surface)
    inputs = [
        "Compression", "FoamTexture", "WaterBaseColor", "WaterOpacity",
        "WaterRoughness", "Enabled", "Intensity", "TexturePower",
        "FoamColor", "OpacityTarget", "RoughnessTarget"]
    if not helper.configure_gerstner_foam_surface_custom_expression(
            surface, inputs, CUSTOM_CODE, SURFACE_DESC,
            ["/Project/SWGerstnerCompression.ush"]):
        raise RuntimeError("Could not configure lit foam Custom")

    connect(editing, compression, "", surface, "Compression")
    connect(editing, foam_texture, "", surface, "FoamTexture")
    connect(editing, water_base_color, "", surface, "WaterBaseColor")
    connect(editing, water_attributes, "Opacity", surface, "WaterOpacity")
    connect(editing, water_attributes, "Roughness", surface, "WaterRoughness")
    for input_name, parameter in parameters.items():
        output = "RGB" if input_name == "FoamColor" else ""
        connect(editing, parameter, output, surface, input_name)

    emissive_zero = find_by_desc(expressions, ZERO_DESC)
    if emissive_zero is None:
        emissive_zero = editing.create_material_expression(
            material, unreal.MaterialExpressionConstant3Vector, 8880, 5340)
        emissive_zero.set_editor_property("desc", ZERO_DESC)
        expressions.append(emissive_zero)
    emissive_zero.set_editor_property("constant", unreal.LinearColor(0.0, 0.0, 0.0, 1.0))

    if not helper.configure_gerstner_foam_attribute_override(
            final_attributes, surface, emissive_zero):
        raise RuntimeError("Could not connect lit foam material attributes")

    legacy_foam.set_editor_property("description", LEGACY_DESC)

    # The former preview switch is no longer part of the render path. Its True
    # input was raw white Emissive, while False was the legacy emissive foam.
    debug_switch = find_parameter(expressions, "SW Gerstner Compression Debug")
    if debug_switch is not None:
        editing.delete_material_expression(material, debug_switch)

    helper.initialize_missing_parameter_guids(material)
    editing.recompile_material(material)
    unreal.EditorAssetLibrary.save_loaded_asset(material, only_if_is_dirty=False)
    unreal.log("SW_GERSTNER_LIT_FOAM_INTEGRATED=1")
    unreal.log("SW_GERSTNER_LIT_FOAM_EMISSIVE=0")
    unreal.log("SW_GERSTNER_LIT_FOAM_TEXTURE_SOURCE=" + short_name(foam_texture))


try:
    main()
except Exception:
    unreal.log_error(traceback.format_exc())
    raise
