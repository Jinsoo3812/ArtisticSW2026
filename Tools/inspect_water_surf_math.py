import re

t3d_path = r'C:\Unreal Projects\ArtisticSW2026\Tools\ExportedMaterials\WaterTextureSurface.t3d'
with open(t3d_path, 'r', encoding='utf-8', errors='ignore') as f:
    text = f.read()

# Let's inspect Add_0 and Normalize_0 to trace inputs
add_0 = re.search(r'Begin Object Name="MaterialExpressionAdd_0"[\s\S]*?End Object', text)
normalize_0 = re.search(r'Begin Object Name="MaterialExpressionNormalize_0"[\s\S]*?End Object', text)

print("=== ADD 0 (Near Normal Addition) ===")
if add_0:
    for line in add_0.group(0).splitlines():
        print(" ", line.strip())

print("\n=== NORMALIZE 0 (Far Normal) ===")
if normalize_0:
    for line in normalize_0.group(0).splitlines():
        print(" ", line.strip())

# Let's see what texture samples are there
tex_samples = re.findall(r'Begin Object Class=/Script/Engine\.MaterialExpressionTextureSample[\s\S]*?End Object', text)
print(f"\nFound {len(tex_samples)} texture sample nodes.")

# Let's list any MaterialFunctionCall nodes
m_func_calls = re.findall(r'Begin Object Class=/Script/Engine\.MaterialExpressionMaterialFunctionCall Name="([^"]+)"[\s\S]*?End Object', text)
print("\n=== MATERIAL FUNCTION CALL BLOCKS ===")
for mfc in m_func_calls:
    block = re.search(fr'Begin Object Name="{mfc}"[\s\S]*?End Object', text)
    if block:
        print(f"Node: {mfc}")
        for line in block.group(0).splitlines():
            if "Function=" in line or "Inputs(" in line:
                print("  ", line.strip())
