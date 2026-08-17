import unreal

material = unreal.load_asset("/Game/New/Water/Realistic_Water/M_Realistic_Water")
prefix = "/Game/New/Water/Realistic_Water/M_Realistic_Water.M_Realistic_Water:"

new_code = """float2 WorldUV = UV / 1500.0; 

// 3방향 일렁임 UV 계산
float3 outOffset = Time * 0.15 + float3(0, 1, 2) / 3.0;
float3 fracTime = frac(outOffset);
float3 weight = (cos(2.0 * 3.14159 * fracTime) * 0.5 + 0.5) * (2.0 / 3.0);

float2 uv1 = WorldUV + outOffset.x * 0.5;
float2 uv2 = WorldUV + outOffset.y * 0.5;
float2 uv3 = WorldUV + outOffset.z * 0.5;

// 1. BaseColor (하얀 거품) 3방향 샘플링
float foam1 = Texture2DSample(FoamHeight, FoamHeightSampler, uv1).r;
float foam2 = Texture2DSample(FoamHeight, FoamHeightSampler, uv2).r;
float foam3 = Texture2DSample(FoamHeight, FoamHeightSampler, uv3).r;
float foamBase = foam1 * weight.x + foam2 * weight.y + foam3 * weight.z;

// 2. ORM의 R채널 (깊은 그림자) 3방향 똑같이 샘플링
float ao1 = Texture2DSample(FoamORM, FoamORMSampler, uv1).r;
float ao2 = Texture2DSample(FoamORM, FoamORMSampler, uv2).r;
float ao3 = Texture2DSample(FoamORM, FoamORMSampler, uv3).r;
float foamAO = ao1 * weight.x + ao2 * weight.y + ao3 * weight.z;

// 3. 원거리에서도 밝기가 죽지 않도록 자연스러운 음영 파워(1.2) 적용!
float shadow = pow(saturate(foamAO), 1.2); 
float finalFoam = foamBase * shadow;

return saturate(finalFoam);"""

mel = unreal.MaterialEditingLibrary

for cn_id in [15, 17]:
    cn = unreal.load_object(None, f"{prefix}MaterialExpressionCustom_{cn_id}")
    if cn:
        cn.set_editor_property("code", new_code)
        unreal.log_warning(f"Updated Custom_{cn_id} code successfully!")

# Recompile and save material
mel.recompile_material(material)
unreal.EditorAssetLibrary.save_loaded_asset(material)
unreal.log_warning("M_Realistic_Water recompiled and saved successfully!")
