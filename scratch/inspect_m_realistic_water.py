import unreal

mat_path = "/Game/New/Water/Realistic_Water/M_Realistic_Water"
mat = unreal.load_asset(mat_path)
if not mat:
    unreal.log_error(f"Failed to load {mat_path}")
    quit()

unreal.log_warning(f"Inspecting Material: {mat.get_name()}")

# Find all expressions using MaterialEditingLibrary or object traversal
mel = unreal.MaterialEditingLibrary

# Find child expressions
sub_objects = unreal.get_default_object(unreal.Package).get_outer() # let's use unreal.SystemLibrary or get expressions via MaterialEditingLibrary
# In UE, unreal.MaterialEditingLibrary has get_child_expression_of_material_function, but for material:
# In python we can find all UMaterialExpression objects that have mat as outer!
all_exprs = [x for x in unreal.find_package(mat.get_package().get_name()).get_objects() if isinstance(x, unreal.MaterialExpression)]

unreal.log_warning(f"Found {len(all_exprs)} MaterialExpressions in package")

# Let's inspect SetMaterialAttributes nodes
set_mat_nodes = []
for expr in all_exprs:
    cname = expr.get_class().get_name()
    desc = expr.get_editor_property("desc") if hasattr(expr, "desc") else ""
    if "SetMaterialAttributes" in cname:
        set_mat_nodes.append(expr)
        unreal.log_warning(f"SetMaterialAttributes Node: {expr.get_name()}, Desc: '{desc}'")

# Let's inspect all Custom nodes and Normal-related nodes
for expr in all_exprs:
    cname = expr.get_class().get_name()
    desc = expr.get_editor_property("desc") if hasattr(expr, "desc") else ""
    if "Custom" in cname:
        code = expr.get_editor_property("code")
        expr_desc = expr.get_editor_property("description")
        inputs = [inp.input_name for inp in expr.get_editor_property("inputs")]
        unreal.log_warning(f"[CustomNode: {expr.get_name()}] Desc='{expr_desc}' / Inputs={inputs}")
        unreal.log_warning(f"   Code snippet: {code[:150]}")

