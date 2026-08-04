import re

with open(r'C:\Unreal Projects\ArtisticSW2026\Tools\ExportedMaterials\V3_M_RealisticWater_GodotInspired.t3d', 'r', encoding='utf-16', errors='ignore') as f:
    text = f.read()

# 1. Comment blocks
comments = re.findall(r'Text="([^"]+)"', text)
print("=== GROUPS / COMMENTS ===")
for c in sorted(list(set(comments))):
    if len(c) < 80 and not c.startswith('MaterialExpression'):
        print("-", c)

# 2. Main material inputs (Look for Object Name="V3_M_RealisticWater_GodotInspired")
pattern = r'Begin Object Name="V3_M_RealisticWater_GodotInspired"[\s\S]*?End Object'
match = re.search(pattern, text)
if match:
    print("\n=== MAIN MATERIAL INPUT CONNECTIONS ===")
    for line in match.group(0).splitlines():
        if "=" in line and not line.strip().startswith("Expressions(") and not line.strip().startswith("EditorComments("):
            print(" ", line.strip())
