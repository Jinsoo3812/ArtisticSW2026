import re

t3d_path = r'C:\Unreal Projects\ArtisticSW2026\Tools\ExportedMaterials\Water_Underside.t3d'
with open(t3d_path, 'r', encoding='utf-8', errors='ignore') as f:
    text = f.read()

# Print all OutputName and InputName
outputs = re.findall(r'OutputName="([^"]+)"', text)
inputs = re.findall(r'InputName="([^"]+)"', text)
print('Outputs:', outputs)
print('Inputs:', inputs)

# Let's inspect objects in the T3D file
obj_classes = re.findall(r'Begin Object Class=([^\s]+) Name="([^"]+)"', text)
print('\n=== OBJECTS ===')
for cls, name in obj_classes:
    if "FunctionOutput" in cls or "FunctionInput" in cls or "TwoSidedSign" in cls or "Switch" in cls or "Custom" in cls:
        print(f"- {cls.split('.')[-1]} : {name}")
        # Print its definition block
        block = re.search(fr'Begin Object Name="{name}"[\s\S]*?End Object', text)
        if block:
            for line in block.group(0).splitlines():
                if "InputName=" in line or "OutputName=" in line or "Expression=" in line or "ParameterName=" in line:
                    print("  ", line.strip())
