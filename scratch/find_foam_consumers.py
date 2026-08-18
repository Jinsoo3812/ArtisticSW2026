import unreal

out_path = r"c:\Unreal Projects\ArtisticSW2026\scratch\foam_consumers_out.txt"
lines = []

MATERIAL_PATH = "/Game/New/Water/Realistic_Water/M_Realistic_Water"
mat = unreal.load_asset(MATERIAL_PATH)
prefix = f"{MATERIAL_PATH}.M_Realistic_Water:"

node15 = unreal.load_object(None, f"{prefix}MaterialExpressionCustom_15")
node17 = unreal.load_object(None, f"{prefix}MaterialExpressionCustom_17")

lines.append(f"Node15: {node15}")
lines.append(f"Node17: {node17}")

classes = [
    "MaterialExpressionCustom",
    "MaterialExpressionSetMaterialAttributes",
    "MaterialExpressionGetMaterialAttributes",
    "MaterialExpressionMaterialFunctionCall",
    "MaterialExpressionTextureSample",
    "MaterialExpressionTextureSampleParameter2D",
    "MaterialExpressionScalarParameter",
    "MaterialExpressionVectorParameter",
    "MaterialExpressionAdd",
    "MaterialExpressionMultiply",
    "MaterialExpressionDivide",
    "MaterialExpressionSubtract",
    "MaterialExpressionLinearInterpolate",
    "MaterialExpressionDotProduct",
    "MaterialExpressionCrossProduct",
    "MaterialExpressionNormalize",
    "MaterialExpressionComponentMask",
    "MaterialExpressionAppendVector",
    "MaterialExpressionConstant",
    "MaterialExpressionConstant2Vector",
    "MaterialExpressionConstant3Vector",
    "MaterialExpressionConstant4Vector",
    "MaterialExpressionPower",
    "MaterialExpressionClamp",
    "MaterialExpressionSaturate",
    "MaterialExpressionSmoothStep",
    "MaterialExpressionIf",
    "MaterialExpressionFresnel",
]

for cname in classes:
    for index in range(200):
        node = unreal.load_object(None, f"{prefix}{cname}_{index}")
        if not node: continue
        
        try:
            inputs = unreal.MaterialEditingLibrary.get_inputs_for_material_expression(mat, node)
            if node15 in inputs or node17 in inputs:
                lines.append(f"\n[FOUND CONSUMER] {node.get_name()} ({cname})")
                for i, inp in enumerate(inputs):
                    if inp == node15:
                        lines.append(f"  - Input[{i}] is Node15")
                    if inp == node17:
                        lines.append(f"  - Input[{i}] is Node17")
        except Exception as e:
            pass

with open(out_path, "w", encoding="utf-8") as f:
    f.write("\n".join(lines))

print("Done writing to " + out_path)
