import json
import os
import unreal


PATH = "/Game/Resources_Assets/Shooter_VFXPack/Materials/M_Projectile"
OUTPUT = os.path.join(unreal.Paths.project_saved_dir(), "m_projectile_graph.json")
material = unreal.EditorAssetLibrary.load_asset(PATH)
if not material:
    raise RuntimeError("Could not load " + PATH)

subsystem = unreal.get_editor_subsystem(unreal.AssetEditorSubsystem)
subsystem.open_editor_for_assets([material])


def safe(fn, default=None):
    try:
        return fn()
    except Exception as exc:
        return {"error": str(exc)} if default is None else default


def inspect(node):
    entry = {
        "id": node.get_path_name(),
        "class": node.get_class().get_name(),
        "name": node.get_name(),
        "input_names": list(safe(lambda: unreal.MaterialEditingLibrary.get_material_expression_input_names(node), [])),
        "properties": {},
    }
    interesting = (
        "parameter_name", "default_value", "constant", "const_a", "const_b",
        "texture", "curve", "atlas", "function", "input_name", "desc",
    )
    for prop in interesting:
        value = safe(lambda prop=prop: node.get_editor_property(prop))
        if not isinstance(value, dict):
            entry["properties"][prop] = str(value)
    return entry


report = {
    "material": material.get_path_name(),
    "num_expressions": unreal.MaterialEditingLibrary.get_num_material_expressions(material),
    "used_textures": [str(x) for x in unreal.MaterialEditingLibrary.get_used_textures(material)],
    "outputs": {},
    "nodes": {},
}

properties = {
    "emissive": unreal.MaterialProperty.MP_EMISSIVE_COLOR,
    "opacity": unreal.MaterialProperty.MP_OPACITY,
    "opacity_mask": unreal.MaterialProperty.MP_OPACITY_MASK,
    "base_color": unreal.MaterialProperty.MP_BASE_COLOR,
}

queue = []
for label, prop in properties.items():
    node = safe(lambda prop=prop: unreal.MaterialEditingLibrary.get_material_property_input_node(material, prop))
    if node and not isinstance(node, dict):
        report["outputs"][label] = node.get_path_name()
        queue.append(node)
    else:
        report["outputs"][label] = str(node)

while queue:
    node = queue.pop(0)
    node_id = node.get_path_name()
    if node_id in report["nodes"]:
        continue
    data = inspect(node)
    inputs = safe(lambda: unreal.MaterialEditingLibrary.get_inputs_for_material_expression(material, node), [])
    inputs = [x for x in inputs if x]
    data["inputs"] = [x.get_path_name() for x in inputs]
    report["nodes"][node_id] = data
    queue.extend(inputs)

with open(OUTPUT, "w", encoding="utf-8") as stream:
    json.dump(report, stream, ensure_ascii=False, indent=2)
unreal.log_warning("M_PROJECTILE_GRAPH=" + OUTPUT)
