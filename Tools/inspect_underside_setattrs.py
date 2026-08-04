import re

t3d_path = r'C:\Unreal Projects\ArtisticSW2026\Tools\ExportedMaterials\Water_Underside.t3d'
with open(t3d_path, 'r', encoding='utf-8', errors='ignore') as f:
    text = f.read()

block = re.search(r'Begin Object Class=/Script/Engine\.MaterialExpressionSetMaterialAttributes Name="MaterialExpressionSetMaterialAttributes_0"[\s\S]*?End Object', text)
if block:
    print("=== SET MATERIAL ATTRIBUTES 0 ===")
    for line in block.group(0).splitlines():
        print(line.strip())

# Let's inspect other nodes like TwoSidedSign connections
multiply_nodes = re.findall(r'Begin Object Class=/Script/Engine\.MaterialExpressionMultiply Name="([^"]+)"[\s\S]*?End Object', text)
print("\n=== MULTIPLY NODES ===")
for m in multiply_nodes:
    m_block = re.search(fr'Begin Object Name="{m}"[\s\S]*?End Object', text)
    if m_block:
        print(f"Node: {m}")
        for l in m_block.group(0).splitlines():
            if "A=" in l or "B=" in l:
                print("  ", l.strip())
