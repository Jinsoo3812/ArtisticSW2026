import unreal

PATH = "/Game/Blueprints/Ship/Skill/Bombardment/M_BombardPreview"
material = unreal.EditorAssetLibrary.load_asset(PATH)
if not material:
    raise RuntimeError("Could not load " + PATH)

editing = unreal.MaterialEditingLibrary
front = editing.get_material_property_input_node(material, unreal.MaterialProperty.MP_FRONT_MATERIAL)
if not front:
    raise RuntimeError("M_BombardPreview has no Front Material expression")

front_inputs = [x for x in editing.get_inputs_for_material_expression(material, front) if x]
if not front_inputs:
    raise RuntimeError("Could not resolve the existing emissive expression")
original_emissive = front_inputs[0]

if str(original_emissive.get_editor_property("desc") or "") == "VortexRangeRingMask":
    sphere = unreal.load_object(None, material.get_path_name() + ":MaterialExpressionSphereMask_0")
    invert = unreal.load_object(None, material.get_path_name() + ":MaterialExpressionOneMinus_0")
    inner_radius = unreal.load_object(None, material.get_path_name() + ":MaterialExpressionScalarParameter_0")
    if not sphere or not invert or not inner_radius:
        raise RuntimeError("Existing ring mask nodes could not be resolved")
    inner_radius.set_editor_property("slider_max", 0.4999)
    if not editing.connect_material_expressions(sphere, "", invert, ""):
        raise RuntimeError("Could not repair SphereMask -> OneMinus connection")
    editing.recompile_material(material)
    material.modify()
    unreal.EditorAssetLibrary.save_loaded_asset(material, only_if_is_dirty=False)
    unreal.log_warning("BOMBARD_PREVIEW_RING_MASK_REPAIRED")
    raise SystemExit(0)

# Idempotent: remove only nodes previously created by this script.
for node in list(front_inputs) + [original_emissive]:
    if node and str(node.get_editor_property("desc") or "") == "VortexRangeRingMask":
        raise RuntimeError("VortexRangeRingMask is already installed")

def make(cls, x, y):
    return editing.create_material_expression(material, cls, x, y)

uv = make(unreal.MaterialExpressionTextureCoordinate, -760, 320)
center = make(unreal.MaterialExpressionConstant2Vector, -760, 440)
center.set_editor_property("r", 0.5)
center.set_editor_property("g", 0.5)

inner_radius = make(unreal.MaterialExpressionScalarParameter, -760, 560)
inner_radius.set_editor_property("parameter_name", "InnerRadius")
inner_radius.set_editor_property("default_value", 0.42)
inner_radius.set_editor_property("slider_min", 0.0)
inner_radius.set_editor_property("slider_max", 0.4999)

sphere = make(unreal.MaterialExpressionSphereMask, -500, 360)
sphere.set_editor_property("hardness_percent", 96.0)
invert = make(unreal.MaterialExpressionOneMinus, -300, 360)
masked_emissive = make(unreal.MaterialExpressionMultiply, -80, 100)
masked_emissive.set_editor_property("desc", "VortexRangeRingMask")

editing.connect_material_expressions(uv, "", sphere, "A")
editing.connect_material_expressions(center, "", sphere, "B")
editing.connect_material_expressions(inner_radius, "", sphere, "Radius")
editing.connect_material_expressions(sphere, "", invert, "")
editing.connect_material_expressions(original_emissive, "", masked_emissive, "A")
editing.connect_material_expressions(invert, "", masked_emissive, "B")
editing.connect_material_expressions(masked_emissive, "", front, "EmissiveColor")

editing.layout_material_expressions(material)
editing.recompile_material(material)
material.modify()
unreal.EditorAssetLibrary.save_loaded_asset(material, only_if_is_dirty=False)
unreal.log_warning("BOMBARD_PREVIEW_RING_MASK_ADDED InnerRadius=0.42")
