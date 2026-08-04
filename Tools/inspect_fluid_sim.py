import re

t3d_path = r'C:\Unreal Projects\ArtisticSW2026\Tools\ExportedMaterials\SampleFluidSimulation.t3d'
with open(t3d_path, 'r', encoding='utf-8', errors='ignore') as f:
    text = f.read()

inputs = re.findall(r'Begin Object Class=/Script/Engine\.MaterialExpressionFunctionInput[\s\S]*?InputName="([^"]+)"[\s\S]*?End Object', text)
outputs = re.findall(r'Begin Object Class=/Script/Engine\.MaterialExpressionFunctionOutput[\s\S]*?OutputName="([^"]+)"[\s\S]*?End Object', text)

print("=== INPUTS ===")
for i in inputs: print("-", i)

print("\n=== OUTPUTS ===")
for o in outputs: print("-", o)

# Check what textures it samples
textures = re.findall(r'Texture="([^"]+)"', text)
print("\n=== SAMPLED TEXTURES ===")
for t in set(textures):
    print("-", t.split("/")[-1].replace("'", ""))

# Check what material functions it calls
funcs = re.findall(r'MaterialFunction="([^"]+)"', text)
func_calls = re.findall(r'Function="([^"]+)"', text)
print("\n=== SUB-FUNCTIONS ===")
for fn in set(funcs + func_calls):
    if "SampleFluidSimulation" not in fn:
        print("-", fn.split("/")[-1].replace("'", ""))
