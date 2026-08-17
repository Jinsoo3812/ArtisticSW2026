import unreal

material = unreal.load_asset("/Game/New/Water/Realistic_Water/M_Realistic_Water")
prefix = "/Game/New/Water/Realistic_Water/M_Realistic_Water.M_Realistic_Water:"

unreal.log_warning("=== M_Realistic_Water Expression Graph Scan ===")

# Scan all possible expression types
classes = [
    "MaterialExpressionSetMaterialAttributes",
    "MaterialExpressionGetMaterialAttributes",
    "MaterialExpressionCustom",
    "MaterialExpressionMaterialFunctionCall",
    "MaterialExpressionTextureSample",
    "MaterialExpressionTextureSampleParameter2D",
    "MaterialExpressionAdd",
    "MaterialExpressionMultiply",
    "MaterialExpressionLinearInterpolate",
    "MaterialExpressionVectorParameter",
    "MaterialExpressionScalarParameter",
    "MaterialExpressionComment",
    "MaterialExpressionTransformPosition",
    "MaterialExpressionTransform",
    "MaterialExpressionVertexNormalWS",
    "MaterialExpressionPixelNormalWS",
    "MaterialExpressionWorldPosition",
    "MaterialExpressionMaterialAttributeLayers",
]

found_nodes = {}
for cname in classes:
    for i in range(1500):
        obj = unreal.load_object(None, f"{prefix}{cname}_{i}")
        if obj:
            found_nodes[obj.get_name()] = (obj, cname)

unreal.log_warning(f"Total found nodes: {len(found_nodes)}")

# Print all SetMaterialAttributes and GetMaterialAttributes
for name, (obj, cname) in found_nodes.items():
    if "SetMaterialAttributes" in cname:
        attrs = obj.get_editor_property("attribute_set_types")
        inputs = list(unreal.MaterialEditingLibrary.get_inputs_for_material_expression(material, obj))
        input_names = [x.get_name() if x else "None" for x in inputs]
        unreal.log_warning(f"[SetMatAttr: {name}] Attrs: {[str(a) for a in attrs]}")
        unreal.log_warning(f"   Inputs: {input_names}")

    elif "GetMaterialAttributes" in cname:
        attrs = obj.get_editor_property("attribute_get_types")
        inputs = list(unreal.MaterialEditingLibrary.get_inputs_for_material_expression(material, obj))
        input_names = [x.get_name() if x else "None" for x in inputs]
        unreal.log_warning(f"[GetMatAttr: {name}] Attrs: {[str(a) for a in attrs]}")
        unreal.log_warning(f"   Inputs: {input_names}")

    elif "Custom" in cname:
        desc = obj.get_editor_property("description")
        code = str(obj.get_editor_property("code"))
        inputs = list(unreal.MaterialEditingLibrary.get_inputs_for_material_expression(material, obj))
        input_names = [x.get_name() if x else "None" for x in inputs]
        unreal.log_warning(f"[Custom: {name}] Desc='{desc}' Inputs={input_names}")
        unreal.log_warning(f"   Code Snippet: {code[:100]}...")

    elif "Comment" in cname:
        text = str(obj.get_editor_property("text"))
        if "LEGACY" in text or "Normal" in text or "Kelvin" in text or "WPO" in text or "Wave" in text:
            unreal.log_warning(f"[Comment: {name}] Text: {text[:80]}")
