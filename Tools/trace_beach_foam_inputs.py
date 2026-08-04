import re

t3d_path = r'C:\Unreal Projects\ArtisticSW2026\Tools\ExportedMaterials\Water_Material_Custom.t3d'
try:
    with open(t3d_path, 'r', encoding='utf-16', errors='ignore') as f:
        text = f.read()
except Exception:
    with open(t3d_path, 'r', encoding='utf-8', errors='ignore') as f:
        text = f.read()

# Find MaterialExpressionMaterialFunctionCall_8 and trace its inputs
# In the previous step, we saw:
# Node: MaterialExpressionMaterialFunctionCall_8
# Function is BeachFoam
block = re.search(r'Begin Object Name="MaterialExpressionMaterialFunctionCall_8"[\s\S]*?End Object', text)
if block:
    print("=== MaterialExpressionMaterialFunctionCall_8 ===")
    for line in block.group(0).splitlines():
        if "FunctionInputs(" in line:
            print("  ", line.strip())

# Find the node referenced in FunctionInputs(0)
# (In my previous output, BeachFoam in WaterTextureSurface had Coordinates, etc.
# But here in Water_Material_Custom, let's see)
