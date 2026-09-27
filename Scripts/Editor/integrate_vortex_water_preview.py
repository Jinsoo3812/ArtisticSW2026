"""Add a local circular color-filter overlay to the completed water surface."""
import traceback
import unreal

MASTER_PATH = "/Game/Blueprints/Water/M_Realistic_Water"
OCEAN_PATH = "/Game/Blueprints/Water/M_Realistic_Water_Ocean"
MPC_PATH = "/Game/Blueprints/Water/MPC_Water_Custom"
GROUP = "SW Vortex Preview"
FINAL_DESC = "SW Vortex Preview Final Attributes"
MASK_DESC = "SW Vortex Preview Circular Mask"

MASK_CODE = r"""return SW_ComputeVortexPreviewMask(
    WorldPosition,
    CenterRadius,
    Enabled,
    EdgeFeather);"""

def short_name(expression):
    return expression.get_path_name().split(":")[-1] if expression else "None"

def prop(expression, name, default=None):
    try:
        return expression.get_editor_property(name)
    except Exception:
        return default

def find_desc(expressions, description):
    return next((e for e in expressions if str(prop(e, "desc", "")) == description), None)

def find_parameter(expressions, name):
    return next((e for e in expressions if str(prop(e, "parameter_name", "")) == name), None)

def make(material, editing, cls, expressions, x, y):
    node = editing.create_material_expression(material, cls, x, y)
    expressions.append(node)
    return node

def scalar(material, editing, expressions, name, default, x, y):
    node = find_parameter(expressions, name)
    if node is None:
        node = make(material, editing, unreal.MaterialExpressionScalarParameter, expressions, x, y)
        node.set_editor_property("parameter_name", name)
    node.set_editor_property("default_value", default)
    node.set_editor_property("group", GROUP)
    return node

def vector(material, editing, expressions, name, default, x, y):
    node = find_parameter(expressions, name)
    if node is None:
        node = make(material, editing, unreal.MaterialExpressionVectorParameter, expressions, x, y)
        node.set_editor_property("parameter_name", name)
    node.set_editor_property("default_value", unreal.LinearColor(*default))
    node.set_editor_property("group", GROUP)
    return node

def connect(editing, source, output_name, target, input_name):
    if not editing.connect_material_expressions(source, output_name, target, input_name):
        raise RuntimeError("Connection failed: {}.{} -> {}.{}".format(
            short_name(source), output_name, short_name(target), input_name))

def main():
    master = unreal.load_asset(MASTER_PATH)
    ocean = unreal.load_asset(OCEAN_PATH)
    collection = unreal.load_asset(MPC_PATH)
    if not master or not ocean or not collection:
        raise RuntimeError("Missing water master, ocean instance, or MPC")

    helper = unreal.RealisticWaterMaterialPipelineLibrary
    editing = unreal.MaterialEditingLibrary
    expressions = list(helper.get_material_expressions(master))
    if not helper.configure_vortex_preview_collection(collection):
        raise RuntimeError("Could not configure vortex preview MPC values")

    existing_final = find_desc(expressions, FINAL_DESC)
    if existing_final:
        unreal.EditorAssetLibrary.save_loaded_asset(collection, only_if_is_dirty=False)
        unreal.log("SW_VORTEX_WATER_PREVIEW_ALREADY_INTEGRATED=1")
        return

    previous_final = editing.get_material_property_input_node(
        master, unreal.MaterialProperty.MP_MATERIAL_ATTRIBUTES)
    if previous_final is None:
        raise RuntimeError("Water material has no Material Attributes output")

    break_attributes = make(master, editing, unreal.MaterialExpressionBreakMaterialAttributes,
                            expressions, 10300, 2700)
    world_position = make(master, editing, unreal.MaterialExpressionWorldPosition,
                          expressions, 9700, 3500)

    center_radius = make(master, editing, unreal.MaterialExpressionCollectionParameter,
                         expressions, 9700, 3660)
    enabled = make(master, editing, unreal.MaterialExpressionCollectionParameter,
                   expressions, 9700, 3820)
    if not helper.configure_collection_parameter_expression(
            center_radius, collection, "SW_VortexPreviewCenterRadius"):
        raise RuntimeError("Could not bind SW_VortexPreviewCenterRadius")
    if not helper.configure_collection_parameter_expression(
            enabled, collection, "SW_VortexPreviewEnabled"):
        raise RuntimeError("Could not bind SW_VortexPreviewEnabled")

    tint_color = vector(master, editing, expressions, "SW Vortex Preview Tint Color",
                        (0.10, 1.00, 0.65, 1.0), 9700, 3980)
    tint_strength = scalar(master, editing, expressions, "SW Vortex Preview Tint Strength",
                           0.35, 9700, 4140)
    emissive_strength = scalar(master, editing, expressions, "SW Vortex Preview Emissive Strength",
                               0.50, 9700, 4300)
    edge_feather = scalar(master, editing, expressions, "SW Vortex Preview Edge Feather",
                          150.0, 9700, 4460)

    mask = make(master, editing, unreal.MaterialExpressionCustom, expressions, 10100, 3600)
    if not helper.configure_float1_custom_expression_with_includes(
            mask, ["WorldPosition", "CenterRadius", "Enabled", "EdgeFeather"],
            MASK_CODE, MASK_DESC, ["/Project/SWVortexPreview.ush"]):
        raise RuntimeError("Could not configure vortex preview mask")
    connect(editing, world_position, "XYZ", mask, "WorldPosition")
    connect(editing, center_radius, "", mask, "CenterRadius")
    connect(editing, enabled, "", mask, "Enabled")
    connect(editing, edge_feather, "", mask, "EdgeFeather")

    tinted = make(master, editing, unreal.MaterialExpressionMultiply, expressions, 10600, 2850)
    alpha = make(master, editing, unreal.MaterialExpressionMultiply, expressions, 10600, 3100)
    base_lerp = make(master, editing, unreal.MaterialExpressionLinearInterpolate, expressions, 10900, 2900)
    emissive_color = make(master, editing, unreal.MaterialExpressionMultiply, expressions, 10600, 3420)
    emissive_scaled = make(master, editing, unreal.MaterialExpressionMultiply, expressions, 10900, 3420)
    emissive_add = make(master, editing, unreal.MaterialExpressionAdd, expressions, 11200, 3320)
    final_attributes = make(master, editing, unreal.MaterialExpressionSetMaterialAttributes,
                            expressions, 11600, 2850)
    final_attributes.set_editor_property("desc", FINAL_DESC)

    connect(editing, previous_final, "", break_attributes, "")
    connect(editing, break_attributes, "BaseColor", tinted, "A")
    connect(editing, tint_color, "RGB", tinted, "B")
    connect(editing, mask, "", alpha, "A")
    connect(editing, tint_strength, "", alpha, "B")
    connect(editing, break_attributes, "BaseColor", base_lerp, "A")
    connect(editing, tinted, "", base_lerp, "B")
    connect(editing, alpha, "", base_lerp, "Alpha")
    connect(editing, tint_color, "RGB", emissive_color, "A")
    connect(editing, mask, "", emissive_color, "B")
    connect(editing, emissive_color, "", emissive_scaled, "A")
    connect(editing, emissive_strength, "", emissive_scaled, "B")
    connect(editing, break_attributes, "EmissiveColor", emissive_add, "A")
    connect(editing, emissive_scaled, "", emissive_add, "B")
    connect(editing, previous_final, "", final_attributes, "MaterialAttributes")
    if not helper.configure_vortex_preview_attribute_override(
            final_attributes, base_lerp, emissive_add):
        raise RuntimeError("Could not configure final vortex material attributes")
    if not editing.connect_material_property(
            final_attributes, "", unreal.MaterialProperty.MP_MATERIAL_ATTRIBUTES):
        raise RuntimeError("Could not connect final material attributes")

    helper.initialize_missing_parameter_guids(master)
    editing.recompile_material(master)
    unreal.EditorAssetLibrary.save_loaded_asset(collection, only_if_is_dirty=False)
    unreal.EditorAssetLibrary.save_loaded_asset(master, only_if_is_dirty=False)
    unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(
        ocean, "SW Vortex Preview Tint Color", unreal.LinearColor(0.10, 1.00, 0.65, 1.0))
    unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(
        ocean, "SW Vortex Preview Tint Strength", 0.35)
    unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(
        ocean, "SW Vortex Preview Emissive Strength", 0.50)
    unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(
        ocean, "SW Vortex Preview Edge Feather", 150.0)
    unreal.EditorAssetLibrary.save_loaded_asset(ocean, only_if_is_dirty=False)
    unreal.log("SW_VORTEX_WATER_PREVIEW_INTEGRATED=1")

try:
    main()
except Exception:
    unreal.log_error(traceback.format_exc())
    raise
