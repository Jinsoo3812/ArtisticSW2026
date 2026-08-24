"""Integrate Improved Foam by editing Custom expressions only where practical.

The existing Gerstner material-function outputs are connected directly to the
Ocean Foam Custom node.  A single Custom expression samples the world-space
Kelvin/Ripple history.  No WPO/Normal path is rewired.
"""

import traceback

import unreal


ASSET_PATH = "/Game/Blueprints/Water/M_Realistic_Water"
STATE_PARAMETER = "SW Improved Foam State"
HISTORY_SAMPLE_DESC = "SW Improved Foam History Sample (Kelvin/Ripple only)"

HISTORY_SAMPLE_CODE = r"""float2 HistoryUV =
    (WorldPos.xy - HistoryGridCenter.xy) / max(HistoryGridSize, 1.0) + 0.5;

if (HistoryUV.x < 0.0 || HistoryUV.x > 1.0 ||
    HistoryUV.y < 0.0 || HistoryUV.y > 1.0)
{
    return 0.0;
}

float2 DistFromCenter = abs(HistoryUV - 0.5) * 2.0;
float EdgeFade = 1.0 - smoothstep(0.80, 1.0, max(DistFromCenter.x, DistFromCenter.y));

// History encoding: R=Kelvin, G=Ripple, B=probabilistic union, A=valid.
float4 HistorySample = ImprovedFoamState.SampleLevel(
    Material.Clamp_WorldGroupSettings, HistoryUV, 0.0);
return saturate(HistorySample.b * EdgeFade);"""

OCEAN_FOAM_CODE = r"""// Gerstner remains on the existing height/steepness Bounds.
float GerstnerHeight = GerstnerWPO.z;
float3 SafeGerstnerNormal = normalize(GerstnerNormal);
float GerstnerSteepness = 1.0 - saturate(SafeGerstnerNormal.z);

float slopeHeight = smoothstep(SlopeBounds.r, SlopeBounds.g, GerstnerHeight);
float slopeSteepness = smoothstep(SlopeBounds.b, SlopeBounds.a, GerstnerSteepness);
float slopeMask = saturate(slopeHeight * slopeSteepness * slopeSteepness);

float crestHeight = smoothstep(CrestBounds.r, CrestBounds.g, GerstnerHeight);
float crestSteepness = smoothstep(CrestBounds.b, CrestBounds.a, GerstnerSteepness);
float crestMask = saturate(crestHeight * crestSteepness);

float3 emeraldNetFoam = ScaleFoam * TranslucentColor * SSSIntensity;
float3 whiteCrestFoam = WhiteFoam * FoamColor;

float3 gerstnerBase = emeraldNetFoam * slopeMask;
float3 gerstnerFoam = lerp(gerstnerBase, whiteCrestFoam, crestMask);

// Kelvin/Ripple history already contains emission, persistence and decay.
float improvedDensity = saturate(ImprovedFoam);
float improvedSoft = smoothstep(0.01, 0.45, improvedDensity);
float improvedWhite = smoothstep(0.35, 0.85, improvedDensity);
float3 improvedBase = emeraldNetFoam * improvedSoft;
float3 improvedFoam = lerp(improvedBase, whiteCrestFoam, improvedWhite);

// Preserve HDR color while preventing overlap from doubling the same Foam layer.
return max(gerstnerFoam, improvedFoam);"""


def expression_name(expression):
    return expression.get_path_name().split(":")[-1]


def find_parameter(expressions, parameter_name):
    for expression in expressions:
        try:
            if str(expression.get_editor_property("parameter_name")) == parameter_name:
                return expression
        except Exception:
            pass
    return None


def find_by_desc(expressions, description):
    for expression in expressions:
        try:
            if str(expression.get_editor_property("desc")) == description:
                return expression
        except Exception:
            pass
    return None


def main():
    material = unreal.load_asset(ASSET_PATH)
    if not material:
        raise RuntimeError("Could not load " + ASSET_PATH)

    helper = unreal.RealisticWaterMaterialPipelineLibrary
    editing = unreal.MaterialEditingLibrary
    expressions = list(helper.get_material_expressions(material))
    by_name = {expression_name(expression): expression for expression in expressions}

    ocean_foam = by_name.get("MaterialExpressionCustom_16")
    if not isinstance(ocean_foam, unreal.MaterialExpressionCustom):
        raise RuntimeError("Ocean Foam Custom_16 was not found")

    original_inputs = []
    for index in range(9):
        upstream = helper.get_connected_input_expression(ocean_foam, index)
        if not upstream:
            raise RuntimeError(f"Ocean Foam input {index} is unexpectedly disconnected")
        original_inputs.append(upstream)

    gerstner = None
    for expression in expressions:
        if not isinstance(expression, unreal.MaterialExpressionMaterialFunctionCall):
            continue
        try:
            function = expression.get_editor_property("material_function")
        except Exception:
            function = None
        if function and "ComputeGerstnerWaves" in function.get_path_name():
            gerstner = expression
            break
    if not gerstner:
        raise RuntimeError("ComputeGerstnerWaves material function was not found")

    world_position = by_name.get("MaterialExpressionWorldPosition_10")
    grid_center = find_parameter(expressions, "ShipWakeGridCenter")
    grid_size = find_parameter(expressions, "ShipWakeGridSize")
    if not world_position or not grid_center or not grid_size:
        raise RuntimeError("Kelvin world/grid inputs required by the history sampler were not found")

    state_parameter = find_parameter(expressions, STATE_PARAMETER)
    if not state_parameter:
        state_parameter = editing.create_material_expression(
            material, unreal.MaterialExpressionTextureObjectParameter, 4380, 3260
        )
        state_parameter.set_editor_property("parameter_name", STATE_PARAMETER)
        black_texture = unreal.load_asset("/Engine/EngineResources/Black.Black")
        if not black_texture:
            black_texture = unreal.load_asset("/Engine/EngineResources/DefaultTexture.DefaultTexture")
        state_parameter.set_editor_property("texture", black_texture)
        state_parameter.set_editor_property(
            "sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR
        )

    history_sample = find_by_desc(expressions, HISTORY_SAMPLE_DESC)
    if not history_sample:
        history_sample = editing.create_material_expression(
            material, unreal.MaterialExpressionCustom, 4640, 3060
        )
    if not helper.configure_float1_custom_expression(
        history_sample,
        ["ImprovedFoamState", "WorldPos", "HistoryGridCenter", "HistoryGridSize"],
        HISTORY_SAMPLE_CODE,
        HISTORY_SAMPLE_DESC,
    ):
        raise RuntimeError("Failed to configure Improved Foam history Custom expression")

    if not editing.connect_material_expressions(state_parameter, "", history_sample, "ImprovedFoamState"):
        raise RuntimeError("Failed to connect Improved Foam State")
    if not editing.connect_material_expressions(world_position, "XYZ", history_sample, "WorldPos"):
        raise RuntimeError("Failed to connect history WorldPos")
    if not editing.connect_material_expressions(grid_center, "RGB", history_sample, "HistoryGridCenter"):
        raise RuntimeError("Failed to connect history GridCenter")
    if not editing.connect_material_expressions(grid_size, "", history_sample, "HistoryGridSize"):
        raise RuntimeError("Failed to connect history GridSize")

    input_names = [
        "ScaleFoam", "WhiteFoam", "WaveHeight", "WaveSteepness",
        "FoamColor", "TranslucentColor", "SlopeBounds", "CrestBounds", "SSSIntensity",
        "GerstnerWPO", "GerstnerNormal", "ImprovedFoam",
    ]
    if not helper.configure_float3_custom_expression(
        ocean_foam,
        input_names,
        OCEAN_FOAM_CODE,
        "Gerstner legacy mask + world-space Kelvin/Ripple Foam history",
    ):
        raise RuntimeError("Failed to configure Ocean Foam Custom expression")

    # Vector parameters default to their RGB output when an empty output name is
    # used. Bounds use A for the upper steepness threshold, so preserve RGBA.
    original_output_names = ["", "", "", "", "", "", "RGBA", "RGBA", ""]
    for input_name, upstream, output_name in zip(
        input_names[:9], original_inputs, original_output_names
    ):
        if not editing.connect_material_expressions(upstream, output_name, ocean_foam, input_name):
            raise RuntimeError(f"Failed to restore Ocean Foam input {input_name}")
    if not editing.connect_material_expressions(gerstner, "WPO", ocean_foam, "GerstnerWPO"):
        raise RuntimeError("Failed to connect Gerstner WPO output")
    if not editing.connect_material_expressions(gerstner, "Normal", ocean_foam, "GerstnerNormal"):
        raise RuntimeError("Failed to connect Gerstner Normal output")
    if not editing.connect_material_expressions(history_sample, "", ocean_foam, "ImprovedFoam"):
        raise RuntimeError("Failed to connect Improved Foam density")

    helper.initialize_missing_parameter_guids(material)
    editing.recompile_material(material)
    unreal.EditorAssetLibrary.save_loaded_asset(material, only_if_is_dirty=False)
    unreal.log(
        "[SW-FOAM][MATERIAL] Integrated Custom nodes: "
        f"Ocean={expression_name(ocean_foam)} History={expression_name(history_sample)} "
        f"StateParameter={expression_name(state_parameter)}"
    )


try:
    main()
except Exception:
    unreal.log_error(traceback.format_exc())
    raise
