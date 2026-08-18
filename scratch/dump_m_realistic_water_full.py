import unreal
import os

out_path = r"c:\Unreal Projects\ArtisticSW2026\Optimization\RealisticWater_Analysis_Dump.txt"
lines = []

def log(msg):
    lines.append(str(msg))

mat_path = "/Game/New/Water/Realistic_Water/M_Realistic_Water"
mat = unreal.load_asset(mat_path)
if not mat:
    log(f"FAILED TO LOAD: {mat_path}")
    with open(out_path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))
    quit()

log("="*80)
log(f"MATERIAL: {mat.get_name()}")
log(f"ShadingModel: {mat.get_editor_property('shading_model')}")
log(f"BlendMode: {mat.get_editor_property('blend_mode')}")
log(f"TwoSided: {mat.get_editor_property('two_sided')}")
log("="*80)

prefix = f"{mat_path}.M_Realistic_Water:"

classes = [
    "MaterialExpressionCustom",
    "MaterialExpressionSetMaterialAttributes",
    "MaterialExpressionGetMaterialAttributes",
    "MaterialExpressionMaterialFunctionCall",
    "MaterialExpressionTextureSample",
    "MaterialExpressionTextureSampleParameter2D",
    "MaterialExpressionTextureObject",
    "MaterialExpressionTextureObjectParameter",
    "MaterialExpressionScalarParameter",
    "MaterialExpressionVectorParameter",
    "MaterialExpressionStaticSwitchParameter",
    "MaterialExpressionStaticBoolParameter",
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
    "MaterialExpressionPanner",
    "MaterialExpressionRotator",
    "MaterialExpressionTime",
    "MaterialExpressionWorldPosition",
    "MaterialExpressionPixelNormalWS",
    "MaterialExpressionVertexNormalWS",
    "MaterialExpressionCameraPositionWS",
    "MaterialExpressionSceneDepth",
    "MaterialExpressionPixelDepth",
    "MaterialExpressionDistance",
    "MaterialExpressionPower",
    "MaterialExpressionSquareRoot",
    "MaterialExpressionAbs",
    "MaterialExpressionClamp",
    "MaterialExpressionSaturate",
    "MaterialExpressionSine",
    "MaterialExpressionCosine",
    "MaterialExpressionFrac",
    "MaterialExpressionFloor",
    "MaterialExpressionCeil",
    "MaterialExpressionSmoothStep",
    "MaterialExpressionStep",
    "MaterialExpressionIf",
    "MaterialExpressionFresnel",
    "MaterialExpressionComment",
]

found_nodes = {}
for cname in classes:
    for i in range(1200):
        obj = unreal.load_object(None, f"{prefix}{cname}_{i}")
        if obj:
            found_nodes[obj.get_name()] = (obj, cname)

log(f"TOTAL FOUND NODES: {len(found_nodes)}")

custom_nodes = []
parameters = []
texture_samples = []
function_calls = []
set_mat_attrs = []
comments = []

for name, (obj, cname) in found_nodes.items():
    if "Custom" in cname:
        custom_nodes.append(obj)
    elif "Parameter" in cname:
        parameters.append(obj)
    elif "TextureSample" in cname or "TextureObject" in cname:
        texture_samples.append(obj)
    elif "MaterialFunctionCall" in cname:
        function_calls.append(obj)
    elif "SetMaterialAttributes" in cname or "GetMaterialAttributes" in cname:
        set_mat_attrs.append(obj)
    elif "Comment" in cname:
        comments.append(obj)

log(f"Custom Nodes: {len(custom_nodes)}")
log(f"Parameters: {len(parameters)}")
log(f"Texture Samples/Objects: {len(texture_samples)}")
log(f"Function Calls: {len(function_calls)}")
log(f"Set/Get Mat Attributes: {len(set_mat_attrs)}")
log(f"Comments: {len(comments)}")
log("="*80)

log("\n--- [1] CUSTOM NODES DETAIL ---")
for cn in custom_nodes:
    desc = cn.get_editor_property("description")
    code = str(cn.get_editor_property("code"))
    includes = list(cn.get_editor_property("include_file_paths"))
    inputs = []
    for inp in cn.get_editor_property("inputs"):
        try:
            inputs.append(str(inp.get_editor_property("input_name")))
        except Exception:
            inputs.append(str(inp))
    log(f"\n[CustomNode: {cn.get_name()}]")
    log(f"  Description: '{desc}'")
    log(f"  Includes: {includes}")
    log(f"  Inputs: {inputs}")
    log(f"  Code Lines: {len(code.splitlines())}")
    log(f"  Code Preview:\n{code}")

log("\n" + "="*80)
log("--- [2] MATERIAL FUNCTION CALLS ---")
for fc in function_calls:
    fn = fc.get_editor_property("material_function")
    fn_name = fn.get_name() if fn else "None"
    log(f"  {fc.get_name()} -> Func: {fn_name} ({fn.get_path_name() if fn else ''})")

log("\n" + "="*80)
log("--- [3] SET/GET MATERIAL ATTRIBUTES ---")
for sma in set_mat_attrs:
    cname = sma.get_class().get_name()
    if "SetMaterialAttributes" in cname:
        attrs = sma.get_editor_property("attribute_set_types")
        inputs = list(unreal.MaterialEditingLibrary.get_inputs_for_material_expression(mat, sma))
        input_names = [x.get_name() if x else "None" for x in inputs]
        log(f"  {sma.get_name()} [SET] -> Attrs: {[str(a) for a in attrs]}")
        log(f"      Inputs: {input_names}")
    elif "GetMaterialAttributes" in cname:
        attrs = sma.get_editor_property("attribute_get_types")
        inputs = list(unreal.MaterialEditingLibrary.get_inputs_for_material_expression(mat, sma))
        input_names = [x.get_name() if x else "None" for x in inputs]
        log(f"  {sma.get_name()} [GET] -> Attrs: {[str(a) for a in attrs]}")
        log(f"      Inputs: {input_names}")

log("\n" + "="*80)
log("--- [4] TEXTURE SAMPLES / TEXTURE OBJECTS ---")
for ts in texture_samples:
    cname = ts.get_class().get_name()
    tex = ts.get_editor_property("texture") if hasattr(ts, "texture") else "N/A"
    tex_name = tex.get_name() if (tex and tex != "N/A" and hasattr(tex, "get_name")) else str(tex)
    param_name = ts.get_editor_property("parameter_name") if hasattr(ts, "parameter_name") else ""
    sampler_type = ts.get_editor_property("sampler_type") if hasattr(ts, "sampler_type") else ""
    log(f"  {ts.get_name()} ({cname}) - Param: '{param_name}' | Tex: {tex_name} | Sampler: {sampler_type}")

log("\n" + "="*80)
log("--- [5] PARAMETERS (GROUPED) ---")
param_dict = {}
for p in parameters:
    pname = str(p.get_editor_property("parameter_name")) if hasattr(p, "parameter_name") else p.get_name()
    group = str(p.get_editor_property("group")) if hasattr(p, "group") else "None"
    param_dict.setdefault(group, []).append((pname, p.get_class().get_name()))

for group, plist in sorted(param_dict.items()):
    log(f"  [Group: {group}] ({len(plist)} params)")
    for pname, ptype in sorted(plist):
        log(f"    - {pname} ({ptype})")

log("="*80)

with open(out_path, "w", encoding="utf-8") as f:
    f.write("\n".join(lines))

print(f"SUCCESSFULLY SAVED ANALYSIS TO {out_path}")
