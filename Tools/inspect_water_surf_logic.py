import re

t3d_path = r'C:\Unreal Projects\ArtisticSW2026\Tools\ExportedMaterials\WaterTextureSurface.t3d'
with open(t3d_path, 'r', encoding='utf-8', errors='ignore') as f:
    text = f.read()

# Let's find comments
comments = re.findall(r'Begin Object Class=/Script/Engine\.MaterialExpressionComment[\s\S]*?Text="([^"]+)"[\s\S]*?End Object', text)
print("=== COMMENTS ===")
for c in comments:
    print("-", c)

# Let's check how the LinearInterpolate nodes are connected
# LinearInterpolate_0 blends normal: let's inspect it
lerp_0 = re.search(r'Begin Object Name="MaterialExpressionLinearInterpolate_0"[\s\S]*?End Object', text)
if lerp_0:
    print("\n=== LERP 0 (Normal Blending) ===")
    for l in lerp_0.group(0).splitlines():
        print(" ", l.strip())

# Lerp_4 blends WPO
lerp_4 = re.search(r'Begin Object Name="MaterialExpressionLinearInterpolate_4"[\s\S]*?End Object', text)
if lerp_4:
    print("\n=== LERP 4 (WPO Blending) ===")
    for l in lerp_4.group(0).splitlines():
        print(" ", l.strip())
