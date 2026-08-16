import unreal


ROOT = "/Game/New/Water/Realistic_Water"
WATER_MATERIAL_PATH = f"{ROOT}/M_Realistic_Water"

WPO_CODE = """float Height;
SW_M5_SAMPLE_GOLDEN_HISTORY(
    WorldPosition.xy,
    ShipWakePreviousHeightField, ShipWakePreviousHeightFieldSampler,
    ShipWakePreviousFieldCenter.xy,
    ShipWakeHeightField, ShipWakeHeightFieldSampler,
    ShipWakeFieldCenter.xy,
    ShipWakeFieldSizeCm,
    ShipWakeHistoryAlpha,
    ShipWakeEnable,
    Height);
return float3(0.0, 0.0, Height);"""


def load(path):
    asset = unreal.load_asset(path)
    if not asset:
        raise RuntimeError(f"Required asset is missing: {path}")
    return asset


def custom_input(name):
    value = unreal.CustomInput()
    value.set_editor_property("input_name", name)
    return value


def connect(source, target, target_input):
    if not unreal.MaterialEditingLibrary.connect_material_expressions(
            source, "", target, target_input):
        raise RuntimeError(
            f"Connection failed: {source.get_name()} -> {target.get_name()}[{target_input}]")


def create_scalar(material, name, default, x, y):
    node = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionScalarParameter, x, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("default_value", default)
    return node


def create_vector(material, name, x, y):
    node = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionVectorParameter, x, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("default_value", unreal.LinearColor(0.0, 0.0, 0.0, 0.0))
    return node


def create_texture(material, name, default, x, y):
    node = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionTextureObjectParameter, x, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("texture", default)
    return node


material = load(WATER_MATERIAL_PATH)
asset_name = WATER_MATERIAL_PATH.rsplit("/", 1)[-1]
prefix = f"{WATER_MATERIAL_PATH}.{asset_name}:"
wpo_custom = None
parameter_nodes = {}
for index in range(600):
    expression = unreal.load_object(None, f"{prefix}MaterialExpressionCustom_{index}")
    if expression:
        description = str(expression.get_editor_property("description"))
        if "SW Kelvin Wake" in description and expression.get_editor_property(
                "output_type") == unreal.CustomMaterialOutputType.CMOT_FLOAT3:
            wpo_custom = expression
    for class_name in (
            "MaterialExpressionTextureObjectParameter",
            "MaterialExpressionVectorParameter",
            "MaterialExpressionScalarParameter"):
        node = unreal.load_object(None, f"{prefix}{class_name}_{index}")
        if node:
            parameter_nodes[str(node.get_editor_property("parameter_name"))] = node

if not wpo_custom:
    raise RuntimeError("Kelvin WPO Custom node was not found in M_Realistic_Water")

old_names = [
    str(value.get_editor_property("input_name"))
    for value in wpo_custom.get_editor_property("inputs")
]
old_nodes = list(unreal.MaterialEditingLibrary.get_inputs_for_material_expression(
    material, wpo_custom))
old_inputs = {
    name: old_nodes[index]
    for index, name in enumerate(old_names)
    if index < len(old_nodes) and old_nodes[index]
}
if "WorldPosition" not in old_inputs:
    raise RuntimeError("Existing Kelvin WPO has no WorldPosition input")

black = unreal.load_asset("/Engine/EngineResources/Black")
if not black:
    black = unreal.load_asset("/Engine/EngineResources/DefaultTexture")

height_field = parameter_nodes.get("ShipWakeHeightField")
if not height_field:
    height_field = old_inputs.get("ShipWakeTex")
if not height_field:
    height_field = create_texture(material, "ShipWakeHeightField", black, 11700, -1150)
height_field.set_editor_property("parameter_name", "ShipWakeHeightField")
height_field.set_editor_property("texture", black)

previous_height_field = parameter_nodes.get("ShipWakePreviousHeightField") or create_texture(
    material, "ShipWakePreviousHeightField", black, 11700, -1350)
previous_height_field.set_editor_property("texture", black)

field_center = parameter_nodes.get("ShipWakeFieldCenter") or create_vector(
    material, "ShipWakeFieldCenter", 11700, -950)
previous_field_center = parameter_nodes.get("ShipWakePreviousFieldCenter") or create_vector(
    material, "ShipWakePreviousFieldCenter", 11700, -1050)
field_size = parameter_nodes.get("ShipWakeFieldSizeCm") or create_scalar(
    material, "ShipWakeFieldSizeCm", 80000.0, 11700, -750)
field_size.set_editor_property("default_value", 80000.0)
history_alpha = parameter_nodes.get("ShipWakeHistoryAlpha") or create_scalar(
    material, "ShipWakeHistoryAlpha", 0.0, 11700, -550)
ship_wake_enable = parameter_nodes.get("ShipWakeEnable") or create_scalar(
    material, "ShipWakeEnable", 1.0, 11700, -350)

wpo_custom.modify()
wpo_custom.set_editor_property("description", "SW Kelvin Wake M5.1 Interpolated Golden History WPO")
wpo_custom.set_editor_property("include_file_paths", ["/Project/SWShipWakeHistory.ush"])
wpo_custom.set_editor_property("inputs", [
    custom_input("WorldPosition"),
    custom_input("ShipWakePreviousHeightField"),
    custom_input("ShipWakePreviousFieldCenter"),
    custom_input("ShipWakeHeightField"),
    custom_input("ShipWakeFieldCenter"),
    custom_input("ShipWakeFieldSizeCm"),
    custom_input("ShipWakeHistoryAlpha"),
    custom_input("ShipWakeEnable"),
])
wpo_custom.set_editor_property("code", WPO_CODE)

for source, input_name in (
        (old_inputs["WorldPosition"], "WorldPosition"),
        (previous_height_field, "ShipWakePreviousHeightField"),
        (previous_field_center, "ShipWakePreviousFieldCenter"),
        (height_field, "ShipWakeHeightField"),
        (field_center, "ShipWakeFieldCenter"),
        (field_size, "ShipWakeFieldSizeCm"),
        (history_alpha, "ShipWakeHistoryAlpha"),
        (ship_wake_enable, "ShipWakeEnable")):
    connect(source, wpo_custom, input_name)

unreal.MaterialEditingLibrary.recompile_material(material)
if not unreal.EditorAssetLibrary.save_loaded_asset(material, only_if_is_dirty=False):
    raise RuntimeError(f"Failed to save: {WATER_MATERIAL_PATH}")

unreal.log_warning(
    "[KelvinM5] PASS "
    f"material={WATER_MATERIAL_PATH} custom={wpo_custom.get_name()} "
    "fields=Previous+Current alpha=ShipWakeHistoryAlpha "
    "include=/Project/SWShipWakeHistory.ush")
