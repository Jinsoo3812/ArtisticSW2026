import unreal


ROOT = "/Game/New/Water/Realistic_Water"
UPDATE_PATH = f"{ROOT}/M_SWShipWakeFieldUpdate"
WATER_PATH = f"{ROOT}/M_Realistic_Water"


def load(path):
    asset = unreal.load_asset(path)
    if not asset:
        raise RuntimeError(f"Missing M3 asset: {path}")
    return asset


def find_custom(material, description_fragment, max_index=240):
    prefix = f"{material.get_path_name()}:{''}"
    # get_path_name() is Package.Asset; expression object paths append ':Name'.
    for index in range(max_index):
        expression = unreal.load_object(
            None, f"{prefix}MaterialExpressionCustom_{index}")
        if not expression:
            continue
        description = str(expression.get_editor_property("description"))
        if description_fragment in description:
            return expression
    return None


def validate_custom(material, custom, expected_inputs, include_path):
    if not custom:
        raise RuntimeError(f"Custom node not found: {material.get_path_name()}")
    actual_inputs = [
        str(value.get_editor_property("input_name"))
        for value in custom.get_editor_property("inputs")
    ]
    if actual_inputs != expected_inputs:
        raise RuntimeError(
            f"Unexpected inputs on {custom.get_name()}: {actual_inputs}")
    includes = [str(value) for value in custom.get_editor_property("include_file_paths")]
    if include_path not in includes:
        raise RuntimeError(f"Missing include on {custom.get_name()}: {includes}")
    connected = list(
        unreal.MaterialEditingLibrary.get_inputs_for_material_expression(material, custom))
    if len(connected) < len(expected_inputs) or any(
            connected[index] is None for index in range(len(expected_inputs))):
        summary = [node.get_name() if node else None for node in connected]
        raise RuntimeError(f"Disconnected Custom input on {custom.get_name()}: {summary}")


update_material = load(UPDATE_PATH)
water_material = load(WATER_PATH)

update_custom = find_custom(update_material, "M3 Signed Height Update")
validate_custom(update_material, update_custom, [
    "UV",
    "PreviousHeightState",
    "CurrentHeightState",
    "ShipWakeTex",
    "ShipWakeServerTime",
    "ShipWakeCount",
    "PreviousStateCenter",
    "CurrentStateCenter",
    "OutputStateCenter",
    "FieldSizeCm",
    "FieldResolution",
    "SimulationDeltaTime",
    "FieldWaveSpeed",
    "FieldDamping",
    "FieldSourceRate",
], "/Project/SWShipWakeField.ush")

update_output = unreal.MaterialEditingLibrary.get_material_property_input_node(
    update_material, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
if update_output != update_custom:
    raise RuntimeError(
        f"Update material Emissive is not driven by M3 Custom: {update_output}")

wpo_custom = find_custom(water_material, "M3 Signed Height Field WPO")
validate_custom(water_material, wpo_custom, [
    "WorldPosition",
    "ShipWakeHeightField",
    "ShipWakeFieldCenter",
    "ShipWakeFieldSizeCm",
    "HeightScale",
    "Enable",
], "/Project/SWShipWakeField.ush")

final_attributes = unreal.MaterialEditingLibrary.get_material_property_input_node(
    water_material, unreal.MaterialProperty.MP_MATERIAL_ATTRIBUTES)
if not final_attributes:
    raise RuntimeError("M_Realistic_Water has no Material Attributes output")
names = [
    str(value)
    for value in unreal.MaterialEditingLibrary.get_material_expression_input_names(final_attributes)
]
nodes = list(
    unreal.MaterialEditingLibrary.get_inputs_for_material_expression(
        water_material, final_attributes))
try:
    wpo_index = names.index("World Position Offset")
except ValueError as exc:
    raise RuntimeError(f"Final SetMaterialAttributes has no WPO input: {names}") from exc
wpo_add = nodes[wpo_index] if wpo_index < len(nodes) else None
if not isinstance(wpo_add, unreal.MaterialExpressionAdd):
    raise RuntimeError(f"Final WPO is not an Add node: {wpo_add}")
add_inputs = list(
    unreal.MaterialEditingLibrary.get_inputs_for_material_expression(
        water_material, wpo_add))
if wpo_custom not in add_inputs:
    raise RuntimeError("M3 WPO Custom is not connected to the final WPO Add")

unreal.MaterialEditingLibrary.recompile_material(update_material)
unreal.MaterialEditingLibrary.recompile_material(water_material)
unreal.log_warning(
    "[KelvinM3Validation] PASS "
    f"update={update_custom.get_name()} water={wpo_custom.get_name()} "
    f"final_wpo_add={wpo_add.get_name()}")
