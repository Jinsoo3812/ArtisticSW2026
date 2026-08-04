import re

t3d_path = r'C:\Unreal Projects\ArtisticSW2026\Tools\ExportedMaterials\V3_M_RealisticWater_GodotInspired.t3d'
with open(t3d_path, 'r', encoding='utf-16', errors='ignore') as f:
    text = f.read()

# Let's search for the MaterialFunctionCall nodes
fn_calls = re.findall(r'Begin Object Class=/Script/Engine\.MaterialExpressionMaterialFunctionCall Name="([^"]+)"[\s\S]*?End Object', text)

targets = ["Water_Underside", "SetWaveAttributes", "WaterRiverFlowmaps", "BeachFoam"]
for fn in fn_calls:
    block = re.search(fr'Begin Object Name="{fn}"[\s\S]*?End Object', text)
    if block:
        block_text = block.group(0)
        if any(t in block_text for t in targets):
            print(f"=== {fn} ===")
            for line in block_text.splitlines():
                if "MaterialFunction=" in line or "Inputs(" in line or "FunctionInputs(" in line:
                    print("  ", line.strip())
