import re

t3d_path = r'C:\Unreal Projects\ArtisticSW2026\Tools\ExportedMaterials\Water_Material_Custom.t3d'
try:
    with open(t3d_path, 'r', encoding='utf-16', errors='ignore') as f:
        text = f.read()
except Exception:
    with open(t3d_path, 'r', encoding='utf-8', errors='ignore') as f:
        text = f.read()

def trace_node(name):
    block = re.search(fr'Begin Object Name="{name}"[\s\S]*?End Object', text)
    if block:
        print(f"=== {name} ===")
        for line in block.group(0).splitlines():
            if "Input=" in line or "Expression=" in line or "ParameterName=" in line or "A=" in line or "B=" in line:
                print("  ", line.strip())

trace_node("MaterialExpressionStaticSwitchParameter_0")
trace_node("MaterialExpressionReroute_2")
