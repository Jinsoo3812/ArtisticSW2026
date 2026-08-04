import re

t3d_path = r'C:\Unreal Projects\ArtisticSW2026\Tools\ExportedMaterials\WaterTextureSurface.t3d'
try:
    with open(t3d_path, 'r', encoding='utf-16', errors='ignore') as f:
        text = f.read()
except Exception:
    with open(t3d_path, 'r', encoding='utf-8', errors='ignore') as f:
        text = f.read()
if len(text) < 100: # If empty or too short, try fallback
    with open(t3d_path, 'r', encoding='utf-8', errors='ignore') as f:
        text = f.read()

# 1. Output/Input nodes
inputs = re.findall(r'Begin Object Class=/Script/Engine\.MaterialExpressionFunctionInput[\s\S]*?InputName="([^"]+)"[\s\S]*?End Object', text)
outputs = re.findall(r'Begin Object Class=/Script/Engine\.MaterialExpressionFunctionOutput[\s\S]*?OutputName="([^"]+)"[\s\S]*?End Object', text)

# 2. Comments/Groups
comments = re.findall(r'Text="([^"]+)"', text)

# 3. Node list
node_types = re.findall(r'Begin Object Class=[^\s]+(MaterialExpression[A-Za-z0-9_]+)', text)
counts = {}
for nt in node_types:
    counts[nt] = counts.get(nt, 0) + 1

print("=== INPUTS ===")
for i in inputs: print("-", i)

print("\n=== OUTPUTS ===")
for o in outputs: print("-", o)

print("\n=== GROUPS (COMMENTS) ===")
for c in sorted(list(set(comments))):
    if len(c) < 100 and not c.startswith('MaterialExpression'):
        print("-", c)

print("\n=== NODE COUNTS ===")
for k, v in sorted(counts.items(), key=lambda x: x[1], reverse=True)[:15]:
    print(f"- {k.replace('MaterialExpression', '')}: {v}")
