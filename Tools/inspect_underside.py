import re

t3d_path = r'C:\Unreal Projects\ArtisticSW2026\Tools\ExportedMaterials\Water_Underside.t3d'
with open(t3d_path, 'r', encoding='utf-8', errors='ignore') as f:
    text = f.read()

# Let's find inputs/outputs of Water_Underside
inputs = re.findall(r'Begin Object Class=/Script/Engine\.MaterialExpressionFunctionInput[\s\S]*?InputName="([^"]+)"[\s\S]*?End Object', text)
outputs = re.findall(r'Begin Object Class=/Script/Engine\.MaterialExpressionFunctionOutput[\s\S]*?OutputName="([^"]+)"[\s\S]*?End Object', text)
comments = re.findall(r'Text="([^"]+)"', text)

# Find SetMaterialAttributes inside Water_Underside
set_mats = re.findall(r'Begin Object Class=/Script/Engine\.MaterialExpressionSetMaterialAttributes[\s\S]*?End Object', text)

print("=== INPUTS ===")
for i in inputs: print("-", i)

print("\n=== OUTPUTS ===")
for o in outputs: print("-", o)

print("\n=== GROUPS (COMMENTS) ===")
for c in set(comments):
    if len(c) < 100 and not c.startswith('MaterialExpression'):
        print("-", c)

if set_mats:
    print("\n=== SET MATERIAL ATTRIBUTES ===")
    for line in set_mats[0].splitlines():
        if "Inputs(" in line:
            print("  ", line.strip())

# Let's inspect what changes are made to the input Attributes (MA)
# Wait, let's write out the properties that are set.
# Typically, Water_Underside checks if the camera is underwater or if TwoSided rendering is enabled,
# and modifies the Base Color, Normal, Roughness, or Opacity for the UNDERSIDE (backface) of the water mesh.
# Let's print out what properties it modifies.
