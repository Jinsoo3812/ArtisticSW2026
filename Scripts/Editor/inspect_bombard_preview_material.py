import json
import os
import unreal

PATH = "/Game/Blueprints/Ship/Skill/Bombardment/M_BombardPreview"
OUTPUT = os.path.join(unreal.Paths.project_saved_dir(), "bombard_preview_material.json")
material = unreal.EditorAssetLibrary.load_asset(PATH)
if not material:
    raise RuntimeError("Could not load " + PATH)

editing = unreal.MaterialEditingLibrary
nodes = []
queue = []
for prop in unreal.MaterialProperty:
    node = editing.get_material_property_input_node(material, prop)
    if node:
        queue.append(node)
seen = set()
while queue:
    node = queue.pop(0)
    if node.get_path_name() in seen:
        continue
    seen.add(node.get_path_name())
    inputs = [x.get_path_name() for x in editing.get_inputs_for_material_expression(material, node) if x]
    data = {
        "path": node.get_path_name(),
        "class": node.get_class().get_name(),
        "inputs": inputs,
        "input_names": list(editing.get_material_expression_input_names(node)),
    }
    for prop in ("parameter_name", "default_value", "constant", "desc"):
        try:
            data[prop] = str(node.get_editor_property(prop))
        except Exception:
            pass
    nodes.append(data)
    queue.extend([x for x in editing.get_inputs_for_material_expression(material, node) if x])

outputs = {}
for prop in unreal.MaterialProperty:
    node = editing.get_material_property_input_node(material, prop)
    if node:
        outputs[str(prop)] = node.get_path_name()

with open(OUTPUT, "w", encoding="utf-8") as stream:
    json.dump({"class": material.get_class().get_name(), "outputs": outputs, "nodes": nodes}, stream, indent=2)
unreal.log_warning("BOMBARD_PREVIEW_REPORT=" + OUTPUT)
