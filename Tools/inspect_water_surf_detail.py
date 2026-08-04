import re

t3d_path = r'C:\Unreal Projects\ArtisticSW2026\Tools\ExportedMaterials\WaterTextureSurface.t3d'
with open(t3d_path, 'r', encoding='utf-8', errors='ignore') as f:
    text = f.read()

# Let's find all Begin Object ... End Object blocks and parse them
blocks = re.findall(r'Begin Object Class=([^\s]+) Name="([^"]+)"[\s\S]*?End Object', text)

texture_refs = re.findall(r'Texture="([^"]+)"', text)
function_refs = re.findall(r'MaterialFunction="([^"]+)"', text) # wait, maybe "Function="?
func_refs = re.findall(r'Function="([^"]+)"', text)
expr_func_calls = re.findall(r'MaterialFunctionCall[\s\S]*?MaterialFunction=([^\n]+)', text)

print("=== TEXTURE REFERENCES ===")
for tr in set(texture_refs):
    print("-", tr.split("/")[-1].replace("'", ""))

print("\n=== FUNCTION REFERENCES (via calls) ===")
for fr in set(func_refs):
    if "WaterTextureSurface" not in fr:
        print("-", fr.split("/")[-1].replace("'", ""))
for efc in set(expr_func_calls):
    print("- Expression Call:", efc.strip())

# Let's check SetMaterialAttributes inputs to see what's being output
set_mats = re.findall(r'Begin Object Name="MaterialExpressionSetMaterialAttributes_0"[\s\S]*?End Object', text)
if set_mats:
    print("\n=== SET MATERIAL ATTRIBUTES 0 (OUTPUT PACKING) ===")
    for line in set_mats[0].splitlines():
        if "Inputs(" in line:
            print(" ", line.strip())
