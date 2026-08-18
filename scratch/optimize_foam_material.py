import unreal

MATERIAL_PATH = "/Game/New/Water/Realistic_Water/M_Realistic_Water"
mat = unreal.load_asset(MATERIAL_PATH)
if not mat:
    raise RuntimeError(f"Failed to load material: {MATERIAL_PATH}")

prefix = f"{MATERIAL_PATH}.M_Realistic_Water:"

node15 = unreal.load_object(None, f"{prefix}MaterialExpressionCustom_15")
node16 = unreal.load_object(None, f"{prefix}MaterialExpressionCustom_16")
node17 = unreal.load_object(None, f"{prefix}MaterialExpressionCustom_17")

print("Before Optimization:")
print(f"  Node15: {node15}")
print(f"  Node16: {node16}")
print(f"  Node17: {node17}")

mel = unreal.MaterialEditingLibrary

# Connect node15 output to node16's "ScaleFoam" input
success_scale = mel.connect_material_expressions(node15, "", node16, "ScaleFoam")
print(f"Connected Node15 -> Node16 (ScaleFoam): {success_scale}")

# Connect node15 output to node16's "WhiteFoam" input (ensure it's also connected)
success_white = mel.connect_material_expressions(node15, "", node16, "WhiteFoam")
print(f"Connected Node15 -> Node16 (WhiteFoam): {success_white}")

if node17:
    # Delete duplicate Node17 from material
    del_success = mel.delete_material_expression(mat, node17)
    print(f"Deleted duplicate Node17: {del_success}")

# Recompile material
mel.recompile_material(mat)

# Save material package
saved = unreal.EditorAssetLibrary.save_asset(MATERIAL_PATH)
print(f"Material saved: {saved}")

print("\nFoam Material Optimization Completed Successfully!")
