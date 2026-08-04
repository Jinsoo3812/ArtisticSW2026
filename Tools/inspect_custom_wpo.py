import re

t3d_path = r'C:\Unreal Projects\ArtisticSW2026\Tools\ExportedMaterials\Water_Material_Custom.t3d'
try:
    with open(t3d_path, 'r', encoding='utf-16', errors='ignore') as f:
        text = f.read()
except Exception:
    with open(t3d_path, 'r', encoding='utf-8', errors='ignore') as f:
        text = f.read()

# Find StaticSwitchParameter with ParameterName="Enable Ocean Foam"
switches = re.findall(r'Begin Object Class=/Script/Engine\.MaterialExpressionStaticSwitchParameter Name="([^"]+)"[\s\S]*?End Object', text)

print("=== STATIC SWITCHES ===")
for sw in switches:
    block = re.search(fr'Begin Object Name="{sw}"[\s\S]*?End Object', text)
    if block:
        block_text = block.group(0)
        if "Enable Ocean Foam" in block_text or "Ocean Foam" in block_text:
            print(f"Switch: {sw}")
            for line in block_text.splitlines():
                if "ParameterName=" in line or "A=" in line or "B=" in line or "Input=" in line or "DefaultValue=" in line:
                    print("  ", line.strip())

# Find the main Material outputs (what is connected to WorldPositionOffset?)
mat_obj = re.search(r'Begin Object Name="Water_Material_Custom"[\s\S]*?End Object', text)
if not mat_obj:
    mat_obj = re.search(r'Begin Object Class=/Script/Engine\.Material Name="([^"]+)"[\s\S]*?End Object', text)
if mat_obj:
    print("\n=== MAIN MATERIAL WPO CONNECTION ===")
    for line in mat_obj.group(0).splitlines():
        if "WorldPositionOffset=" in line:
            print("  ", line.strip())
