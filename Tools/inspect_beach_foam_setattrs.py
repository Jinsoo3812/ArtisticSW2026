import re

t3d_path = r'C:\Unreal Projects\ArtisticSW2026\Tools\ExportedMaterials\BeachFoam.t3d'
with open(t3d_path, 'r', encoding='utf-8', errors='ignore') as f:
    text = f.read()

# Match the initialization block
# We search for Begin Object Name="MaterialExpressionSetMaterialAttributes_0"
# but ensuring it doesn't have Class=
blocks = re.findall(r'Begin Object Name="MaterialExpressionSetMaterialAttributes_0"[\s\S]*?End Object', text)
for b in blocks:
    print("=== SetMaterialAttributes_0 ===")
    print(b)
