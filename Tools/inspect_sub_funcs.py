import re

t3d_path = r'C:\Unreal Projects\ArtisticSW2026\Tools\ExportedMaterials\WaterTextureSurface.t3d'
with open(t3d_path, 'r', encoding='utf-8', errors='ignore') as f:
    text = f.read()

funcs = re.findall(r'MaterialFunction\'"([^"]+)"\'', text)
print("=== CALLED FUNCTIONS ===")
for fn in set(funcs):
    print('-', fn)

# Let's inspect objects of type MaterialExpressionTextureObject or similar
texture_objects = re.findall(r'Begin Object Class=/Script/Engine\.MaterialExpressionTextureObject[\s\S]*?Texture=Texture2D\'"([^"]+)"\'[\s\S]*?End Object', text)
print("\n=== TEXTURE OBJECTS ===")
for to in set(texture_objects):
    print('-', to)
