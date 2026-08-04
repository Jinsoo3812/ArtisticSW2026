import re

t3d_path = r'C:\Unreal Projects\ArtisticSW2026\Tools\ExportedMaterials\V3_M_RealisticWater_GodotInspired.t3d'
with open(t3d_path, 'r', encoding='utf-16', errors='ignore') as f:
    text = f.read()

# Comments
comments = re.findall(r'Text="([^"]+)"', text)

# Material Functions
functions = re.findall(r'MaterialFunction\'"([^"]+)"\'', text)

# Custom Nodes
custom_matches = re.findall(r'Begin Object Class=/Script/Engine\.MaterialExpressionCustom[\s\S]*?End Object', text)
custom_info = []
for cm in custom_matches:
    desc = re.search(r'Description="([^"]+)"', cm)
    code = re.search(r'Code="([^"]+)"', cm)
    desc_str = desc.group(1) if desc else "Custom"
    code_str = code.group(1) if code else ""
    custom_info.append((desc_str, code_str))

# Main Material Connections (Inputs to Material Root or SingleLayerWaterOutput)
slw_output = re.findall(r'Begin Object Class=/Script/Engine\.MaterialExpressionSingleLayerWaterMaterialOutput[\s\S]*?End Object', text)

print('=== COMMENTS / GROUPS ===')
seen_c = set()
for c in comments:
    if c not in seen_c and len(c) < 150 and not c.startswith('MaterialExpression'):
        seen_c.add(c)
        print('-', c)

print('\n=== MATERIAL FUNCTIONS ===')
for fn in set(functions):
    print('-', fn.split('/')[-1])

print('\n=== CUSTOM HLSL NODES ===')
for desc, code in custom_info:
    print(f'-- {desc} --')
    print(code[:300])
    print('...')

print('\n=== SINGLE LAYER WATER OUTPUT CONNECTIONS ===')
if slw_output:
    print(slw_output[0])
