import unreal


MATERIAL_PATH = "/Game/New/Water/Realistic_Water/M_Realistic_Water"
material = unreal.load_asset(MATERIAL_PATH)
if not material:
    raise RuntimeError(f"Missing material: {MATERIAL_PATH}")

prefix = f"{MATERIAL_PATH}.M_Realistic_Water:"
custom = None
for index in range(260):
    candidate = unreal.load_object(None, f"{prefix}MaterialExpressionCustom_{index}")
    if candidate and "M4 Golden Atlas WPO" in str(
            candidate.get_editor_property("description")):
        custom = candidate
        break
if not custom:
    raise RuntimeError("M4 Kelvin WPO Custom node is missing")

expected = [
    "WorldPosition", "ShipWakeTex", "ShipWakeTrajectoryTex", "ShipWakeAtlas",
    "ShipWakeServerTime", "ShipWakeCount",
]
actual = [str(value.get_editor_property("input_name"))
          for value in custom.get_editor_property("inputs")]
if actual != expected:
    raise RuntimeError(f"Unexpected M4 inputs: {actual}")
includes = [str(value) for value in custom.get_editor_property("include_file_paths")]
if "/Project/SWShipWake.ush" not in includes:
    raise RuntimeError(f"M4 include is missing: {includes}")
connected = list(unreal.MaterialEditingLibrary.get_inputs_for_material_expression(
    material, custom))
if len(connected) < len(expected) or any(node is None for node in connected[:len(expected)]):
    raise RuntimeError("M4 Custom node has a disconnected input")

unreal.MaterialEditingLibrary.recompile_material(material)
unreal.log_warning(
    f"[KelvinM4Validation] PASS custom={custom.get_name()} inputs={len(expected)}")
