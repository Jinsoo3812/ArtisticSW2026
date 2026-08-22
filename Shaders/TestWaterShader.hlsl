// Standalone HLSL test for SWShipWake
#define SW_M7_WAKE_CAPACITY 256
#define SW_M7_GOLDEN_PEAK 1.3871971383
#define SW_M7_DOMAIN_U_RANGE 10.0
#define SW_M7_DOMAIN_V_MAX 3.0

// Unreal HLSL helper replacements
#define Texture2DSampleLevel(tex, samp, uv, lod) tex.SampleLevel(samp, uv, lod)

#include "SWShipWake.ush"

Texture2D EventTex : register(t0);
SamplerState EventSampler : register(s0);
Texture2D GoldenTex : register(t1);
SamplerState GoldenSampler : register(s1);

struct PS_INPUT
{
    float4 Pos : SV_POSITION;
    float2 UV : TEXCOORD0;
};

float4 MainPS(PS_INPUT input) : SV_Target
{
    float2 WorldXY = input.UV * 10000.0;
    float OutHeight = 0.0;
    float3 OutNormal = float3(0, 0, 1);
    
    // Test the optimized unified combined evaluator
    SW_M7_EVALUATE_KELVIN_COMBINED(WorldXY, 10.0, 16.0, EventTex, EventSampler, GoldenTex, GoldenSampler, 1.0, OutHeight, OutNormal);
    
    return float4(OutNormal.xy, OutHeight, 1.0);
}
