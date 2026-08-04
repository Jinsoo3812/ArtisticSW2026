import re

t3d_path = r'C:\Unreal Projects\ArtisticSW2026\Tools\ExportedMaterials\Water_Material_Custom.t3d'
try:
    with open(t3d_path, 'r', encoding='utf-16', errors='ignore') as f:
        text = f.read()
except Exception:
    with open(t3d_path, 'r', encoding='utf-8', errors='ignore') as f:
        text = f.read()

# Let's check the main Material class properties block again
lines = text.splitlines()
in_main = False
main_lines = []
for line in lines:
    if 'Begin Object Class=/Script/Engine.Material Name=' in line:
        in_main = True
    if in_main:
        main_lines.append(line)
        if 'End Object' in line and len(main_lines) > 5:
            # We want to find the final "End Object" for the main material
            pass

# Let's search for SetMaterialAttributes or WPO in the entire file
set_mats = re.findall(r'SetMaterialAttributes[\s\S]*?World Position Offset[\s\S]*?End Object', text)
print(f"SetMaterialAttributes with WPO: {len(set_mats)}")

# Let's inspect SetMaterialAttributes inputs in Water_Material_Custom.t3d
set_mat_nodes = re.findall(r'Begin Object Class=/Script/Engine\.MaterialExpressionSetMaterialAttributes Name="([^"]+)"[\s\S]*?End Object', text)
for sm in set_mat_nodes:
    block = re.search(fr'Begin Object Name="{sm}"[\s\S]*?End Object', text)
    if block:
        print(f"\nSetMaterialAttributes Node: {sm}")
        for l in block.group(0).splitlines():
            if "InputName=" in l or "Expression=" in l:
                print("  ", l.strip())
