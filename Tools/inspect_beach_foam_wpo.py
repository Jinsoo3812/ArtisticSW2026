import re

t3d_path = r'C:\Unreal Projects\ArtisticSW2026\Tools\ExportedMaterials\BeachFoam.t3d'
with open(t3d_path, 'r', encoding='utf-8', errors='ignore') as f:
    text = f.read()

# Let's search for SetMaterialAttributes or WPO references
set_mats = re.findall(r'Begin Object Class=/Script/Engine\.MaterialExpressionSetMaterialAttributes Name="([^"]+)"[\s\S]*?End Object', text)
print("=== SET MATERIAL ATTRIBUTES NODES ===")
for sm in set_mats:
    block = re.search(fr'Begin Object Name="{sm}"[\s\S]*?End Object', text)
    if block:
        print(f"Node: {sm}")
        for line in block.group(0).splitlines():
            if "InputName=" in line or "Expression=" in line:
                print("  ", line.strip())

# Check output pins/nodes
outputs = re.findall(r'Begin Object Class=/Script/Engine\.MaterialExpressionFunctionOutput Name="([^"]+)"[\s\S]*?End Object', text)
print("\n=== OUTPUTS ===")
for o in outputs:
    block = re.search(fr'Begin Object Name="{o}"[\s\S]*?End Object', text)
    if block:
        print(f"Output: {o}")
        for line in block.group(0).splitlines():
            if "OutputName=" in line or "A=" in line:
                print("  ", line.strip())

# Check for GetMaterialAttributes to see what properties it reads from input MA
get_mats = re.findall(r'Begin Object Class=/Script/Engine\.MaterialExpressionGetMaterialAttributes Name="([^"]+)"[\s\S]*?End Object', text)
print("\n=== GET MATERIAL ATTRIBUTES NODES ===")
for gm in get_mats:
    block = re.search(fr'Begin Object Name="{gm}"[\s\S]*?End Object', text)
    if block:
        print(f"Node: {gm}")
        for line in block.group(0).splitlines():
            if "AttributeGetTypes(" in line:
                print("  ", line.strip())
