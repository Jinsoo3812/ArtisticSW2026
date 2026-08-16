import unreal


MATERIAL_PATH = "/Game/New/Water/Realistic_Water/M_Realistic_Water"
material = unreal.load_asset(MATERIAL_PATH)
if not material:
    raise RuntimeError(f"Missing material: {MATERIAL_PATH}")

asset_name = MATERIAL_PATH.rsplit("/", 1)[-1]
prefix = f"{MATERIAL_PATH}.{asset_name}:"
custom = None
for index in range(600):
    candidate = unreal.load_object(None, f"{prefix}MaterialExpressionCustom_{index}")
    if candidate and "M5.1 Interpolated Golden History" in str(
            candidate.get_editor_property("description")):
        custom = candidate
        break
if not custom:
    raise RuntimeError("M5 Golden History Custom node is missing")

expected = [
    "WorldPosition",
    "ShipWakePreviousHeightField",
    "ShipWakePreviousFieldCenter",
    "ShipWakeHeightField",
    "ShipWakeFieldCenter",
    "ShipWakeFieldSizeCm",
    "ShipWakeHistoryAlpha",
    "ShipWakeEnable",
]
actual = [str(item.get_editor_property("input_name")) for item in custom.get_editor_property("inputs")]
if actual != expected:
    raise RuntimeError(f"Unexpected M5 inputs: {actual}")
if list(custom.get_editor_property("include_file_paths")) != ["/Project/SWShipWakeHistory.ush"]:
    raise RuntimeError("M5 include path is incorrect")

connected = list(unreal.MaterialEditingLibrary.get_inputs_for_material_expression(material, custom))
if len(connected) < len(expected) or any(node is None for node in connected[:len(expected)]):
    raise RuntimeError("M5 Custom node has a disconnected input")

final_attributes = unreal.MaterialEditingLibrary.get_material_property_input_node(
    material, unreal.MaterialProperty.MP_MATERIAL_ATTRIBUTES)
if not final_attributes:
    raise RuntimeError("M_Realistic_Water has no Material Attributes output")
input_names = [
    str(value)
    for value in unreal.MaterialEditingLibrary.get_material_expression_input_names(final_attributes)
]
input_nodes = list(unreal.MaterialEditingLibrary.get_inputs_for_material_expression(
    material, final_attributes))
try:
    wpo_index = input_names.index("World Position Offset")
except ValueError as exc:
    raise RuntimeError(f"Final SetMaterialAttributes has no WPO input: {input_names}") from exc
wpo_add = input_nodes[wpo_index] if wpo_index < len(input_nodes) else None
if not isinstance(wpo_add, unreal.MaterialExpressionAdd):
    raise RuntimeError(f"Final WPO is not an Add node: {wpo_add}")
add_inputs = list(unreal.MaterialEditingLibrary.get_inputs_for_material_expression(
    material, wpo_add))
if custom not in add_inputs:
    raise RuntimeError("M5 Custom is not connected to the final Gerstner/Ripple WPO Add")

unreal.log_warning(
    f"[KelvinM5Validate] PASS material={MATERIAL_PATH} custom={custom.get_name()} "
    f"final_wpo_add={wpo_add.get_name()}")
