import re

t3d_path = r'C:\Unreal Projects\ArtisticSW2026\Tools\ExportedMaterials\V3_M_RealisticWater_GodotInspired.t3d'
with open(t3d_path, 'r', encoding='utf-16', errors='ignore') as f:
    text = f.read()

# Find the main Material object definition
mat_obj = re.search(r'Begin Object Class=/Script/Engine\.Material Name="V3_M_RealisticWater_GodotInspired"[\s\S]*?End Object', text)
if mat_obj:
    print("=== MAIN MATERIAL ATTRIBUTE CONNECTIONS ===")
    lines = mat_obj.group(0).splitlines()
    for line in lines:
        if any(attr in line for attr in ["BaseColor", "Metallic", "Specular", "Roughness", "Normal", "WorldPositionOffset", "Opacity", "Refraction", "PixelDepthOffset", "EmissiveColor"]):
            print(line.strip())

# Find SingleLayerWaterMaterialOutput connections
slw_obj = re.search(r'Begin Object Class=/Script/Engine\.MaterialExpressionSingleLayerWaterMaterialOutput[\s\S]*?End Object', text)
if slw_obj:
    print("\n=== SINGLE LAYER WATER OUTPUT NODE INPUTS ===")
    lines = slw_obj.group(0).splitlines()
    for line in lines:
        if "=" in line:
            print(line.strip())
