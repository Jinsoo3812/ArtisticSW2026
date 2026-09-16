import traceback

import unreal


ASSET_PATH = "/Game/Blueprints/Water/M_Realistic_Water"

OLD_CODE = """float OutHeight = 0.0;
float3 OutNormal = float3(0.0, 0.0, 1.0);

if (ShipWakeEnable > 0.5)
{
    // 1. 월드 좌표 -> 렌더타깃 UV(0.0 ~ 1.0) 변환
    float2 GridUV = (WorldPos.xy - ShipWakeGridCenter.xy) / max(ShipWakeGridSize, 1.0) + 0.5;

    // 2. 텍스처 영역(300m) 내부인 경우에만 샘플링
    if (GridUV.x >= 0.0 && GridUV.x <= 1.0 && GridUV.y >= 0.0 && GridUV.y <= 1.0)
    {
        // 외곽 경계선(240m~300m) 부드러운 페이드아웃 마스크
        float2 DistFromCenter = abs(GridUV - 0.5) * 2.0;
        float EdgeFade = 1.0 - smoothstep(0.80, 1.0, max(DistFromCenter.x, DistFromCenter.y));

        // 3. 언리얼 머티리얼 표준 클램프 샘플러로 1회 샘플링 (Mip 0)
        float4 WakeSample = ShipWakeRT.SampleLevel(Material.Clamp_WorldGroupSettings, GridUV, 0.0);

        // R = 파도 높이(WPO), G/B = 노멀 기울기
        OutHeight = WakeSample.r * EdgeFade;
        OutNormal = normalize(float3(WakeSample.g * EdgeFade, WakeSample.b * EdgeFade, 1.0));
    }
}

// RGB: Normal 벡터, A: Height (WPO용 높이)
return float4(OutNormal, OutHeight);
"""

NEW_CODE = """float OutHeight = 0.0;
float3 OutNormal = float3(0.0, 0.0, 1.0);

if (ShipWakeEnable > 0.5)
{
    // 1. 월드 좌표 -> 렌더타깃 UV(0.0 ~ 1.0) 변환
    float2 GridUV = (WorldPos.xy - ShipWakeGridCenter.xy) / max(ShipWakeGridSize, 1.0) + 0.5;

    // 2. 텍스처 영역(300m) 내부인 경우에만 샘플링
    if (GridUV.x >= 0.0 && GridUV.x <= 1.0 && GridUV.y >= 0.0 && GridUV.y <= 1.0)
    {
        // 외곽 경계선(240m~300m) 부드러운 페이드아웃 마스크
        float2 DistFromCenter = abs(GridUV - 0.5) * 2.0;
        float EdgeFade = 1.0 - smoothstep(0.80, 1.0, max(DistFromCenter.x, DistFromCenter.y));

        // 3. Compute RT 인코딩: R = Height, G/B/A = Normal X/Y/Z
        float4 WakeSample = ShipWakeRT.SampleLevel(Material.Clamp_WorldGroupSettings, GridUV, 0.0);

        OutHeight = WakeSample.r * EdgeFade;

        // RT에 저장된 완전한 normal XYZ를 복원한다. 경계에서는 flat normal로 보간한다.
        float3 SampledWakeNormal = normalize(WakeSample.gba);
        OutNormal = normalize(lerp(float3(0.0, 0.0, 1.0), SampledWakeNormal, EdgeFade));
    }
}

// Custom 출력 인코딩: RGB = Normal, A = Height (WPO용 높이)
return float4(OutNormal, OutHeight);
"""


def normalize_newlines(value):
    return value.replace("\r\n", "\n").replace("\r", "\n")


def main():
    material = unreal.load_asset(ASSET_PATH)
    if not material:
        raise RuntimeError("Could not load " + ASSET_PATH)

    helper = unreal.RealisticWaterMaterialPipelineLibrary
    matches = []
    for expression in helper.get_material_expressions(material):
        if not isinstance(expression, unreal.MaterialExpressionCustom):
            continue
        code = expression.get_editor_property("code") or ""
        if "ShipWakeRT.SampleLevel" in code and "return float4(OutNormal, OutHeight)" in code:
            matches.append(expression)

    if len(matches) != 1:
        raise RuntimeError("Expected exactly one Kelvin Custom node, found %d" % len(matches))

    custom = matches[0]
    current = normalize_newlines(custom.get_editor_property("code"))
    expected = normalize_newlines(OLD_CODE)
    replacement = normalize_newlines(NEW_CODE)

    if current == replacement:
        unreal.log("Kelvin wake normal decode is already fixed")
    elif current == expected:
        custom.set_editor_property("code", NEW_CODE)
        custom.set_editor_property(
            "desc", "Kelvin RT decode: R=Height, GBA=Normal XYZ; output RGB=Normal, A=Height"
        )
        unreal.MaterialEditingLibrary.recompile_material(material)
        unreal.EditorAssetLibrary.save_loaded_asset(material, only_if_is_dirty=False)
        unreal.log("Kelvin wake normal decode fixed and material saved")
    else:
        raise RuntimeError(
            "Kelvin Custom code differs from the inspected baseline; refusing a partial replacement"
        )


try:
    main()
except Exception:
    unreal.log_error(traceback.format_exc())
    raise
