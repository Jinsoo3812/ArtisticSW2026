"""Validate the textured, non-emissive Gerstner compression foam path."""
import traceback

import unreal


MASTER_PATH = "/Game/Blueprints/Water/M_Realistic_Water"
SURFACE_TOKEN = "SW_ComposeGerstnerLitFoam"
REQUIRED_PARAMETERS = {
    "SW Gerstner Jacobian Start",
    "SW Gerstner Jacobian Full",
    "SW Gerstner Crest Width",
    "SW Gerstner Crest Sharpness",
    "SW Gerstner Crest Min Wave Amplitude",
    "SW Gerstner Compression Influence",
    "SW Gerstner Foam Enabled",
    "SW Gerstner Foam Intensity",
    "SW Gerstner Foam Texture Power",
    "SW Gerstner Foam Opacity",
    "SW Gerstner Foam Roughness",
    "SW Gerstner Foam Color",
}


def short_name(expression):
    return expression.get_path_name().split(":")[-1] if expression else "None"


def prop(expression, name, default=""):
    try:
        return expression.get_editor_property(name)
    except Exception:
        return default


def main():
    material = unreal.load_asset(MASTER_PATH)
    if material is None:
        raise RuntimeError("Could not load water master")

    helper = unreal.RealisticWaterMaterialPipelineLibrary
    editing = unreal.MaterialEditingLibrary
    expressions = list(helper.get_material_expressions(material))
    by_name = {short_name(e): e for e in expressions}

    surface = next((e for e in expressions
                    if isinstance(e, unreal.MaterialExpressionCustom)
                    and SURFACE_TOKEN in str(prop(e, "code"))), None)
    if surface is None:
        raise RuntimeError("Lit foam Custom is missing")

    compression = next((e for e in expressions
                        if isinstance(e, unreal.MaterialExpressionCustom)
                        and "SW_ComputeGerstnerCrestFoamMask" in str(
                            prop(e, "code"))), None)
    if compression is None:
        raise RuntimeError("Crest/compression mask Custom is missing")
    if "/Project/SWGerstnerCompression.ush" not in str(
            prop(compression, "include_file_paths")):
        raise RuntimeError("Crest mask USH include is missing")
    for index in range(9):
        if helper.get_connected_input_expression(compression, index) is None:
            raise RuntimeError("Crest mask input {} is disconnected".format(index))
    water_time = helper.get_connected_input_expression(compression, 1)
    water_time_function = prop(water_time, "material_function", None)
    water_time_path = (water_time_function.get_path_name()
                       if water_time_function is not None else "")
    if water_time_path != (
            "/Water/Materials/Functions/GetWaterTime.GetWaterTime"):
        raise RuntimeError("Crest mask is not using GetWaterTime")
    if [str(v) for v in helper.get_material_expression_output_names(surface)] != [
            "return", "FoamOpacity", "FoamRoughness"]:
        raise RuntimeError("Unexpected lit foam outputs")
    if "/Project/SWGerstnerCompression.ush" not in str(
            prop(surface, "include_file_paths")):
        raise RuntimeError("Lit foam USH include is missing")
    for index in range(11):
        if helper.get_connected_input_expression(surface, index) is None:
            raise RuntimeError("Lit foam input {} is disconnected".format(index))

    final_attributes = by_name.get("MaterialExpressionSetMaterialAttributes_2")
    if final_attributes is None:
        raise RuntimeError("Final Material Attributes is missing")
    for index in (2, 4, 7):
        if helper.get_connected_input_expression(final_attributes, index) != surface:
            raise RuntimeError("Foam output is missing at final input {}".format(index))
    emissive = helper.get_connected_input_expression(final_attributes, 6)
    if "Gerstner Foam Emissive Disabled" not in str(prop(emissive, "desc")):
        raise RuntimeError("Foam Emissive was not replaced with zero")

    parameter_names = {str(prop(e, "parameter_name")) for e in expressions}
    missing = REQUIRED_PARAMETERS - parameter_names
    if missing:
        raise RuntimeError("Missing parameters: " + ", ".join(sorted(missing)))
    if "SW Gerstner Compression Debug" in parameter_names:
        raise RuntimeError("Legacy raw-white debug switch remains")
    if by_name.get("MaterialExpressionCustom_16") is None:
        raise RuntimeError("Legacy emissive foam node should remain preserved")

    editing.recompile_material(material)
    unreal.log("SW_GERSTNER_LIT_FOAM_VALIDATION=PASS")
    unreal.log("SW_GERSTNER_LIT_FOAM_SURFACE=" + short_name(surface))
    unreal.log("SW_GERSTNER_LIT_FOAM_OUTPUTS=BaseColor,Opacity,Roughness")
    unreal.log("SW_GERSTNER_LIT_FOAM_EMISSIVE=ZERO")


try:
    main()
except Exception:
    unreal.log_error(traceback.format_exc())
    raise
