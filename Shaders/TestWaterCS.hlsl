// Standalone HLSL test for SWShipWakeCS
#define SW_M7_WAKE_CAPACITY 256
#define SW_M7_GOLDEN_PEAK 1.3871971383
#define SW_M7_DOMAIN_U_RANGE 10.0
#define SW_M7_DOMAIN_V_MAX 3.0

#define Texture2DSampleLevel(tex, samp, uv, lod) tex.SampleLevel(samp, uv, lod)

#include "SWShipWake.ush"

Texture2D EventTexture : register(t0);
SamplerState EventTextureSampler : register(s0);

Texture2D GoldenTexture : register(t1);
SamplerState GoldenTextureSampler : register(s1);

RWTexture2D<float4> OutWakeTexture : register(u0);

cbuffer CB : register(b0)
{
    float2 GridCenter;
    float GridSize;
    float ServerTime;
    float EventCount;
    float NormalStrength;
};

[numthreads(16, 16, 1)]
void MainCS(uint3 DTid : SV_DispatchThreadID)
{
    uint2 TexCoord = DTid.xy;
    uint Width, Height;
    OutWakeTexture.GetDimensions(Width, Height);
    if (TexCoord.x >= Width || TexCoord.y >= Height)
    {
        return;
    }

    float2 UV = (float2(TexCoord) + 0.5) / float2(Width, Height);
    float2 WorldXY = GridCenter + (UV - 0.5) * GridSize;

    float OutHeight = 0.0;
    float3 OutNormal = float3(0.0, 0.0, 1.0);

    SW_M7_EVALUATE_KELVIN_COMBINED(
        WorldXY,
        ServerTime,
        EventCount,
        EventTexture,
        EventTextureSampler,
        GoldenTexture,
        GoldenTextureSampler,
        NormalStrength,
        OutHeight,
        OutNormal
    );

    OutWakeTexture[TexCoord] = float4(OutHeight, OutNormal.x, OutNormal.y, OutNormal.z);
}
