import unreal


MATERIAL_PATH = "/Game/New/Water/Realistic_Water/M_Realistic_Water"
SHIP_PATH = "/Game/Tests/Landscape/Kelvin/BP_PlayerShip_Kelvin"
material = unreal.load_asset(MATERIAL_PATH)
ship = unreal.load_asset(SHIP_PATH)
if not material or not ship:
    raise RuntimeError("M7 assets are missing")
unreal.MaterialEditingLibrary.recompile_material(material)

prefix = f"{MATERIAL_PATH}.M_Realistic_Water:"
custom = None
for index in range(900):
    node = unreal.load_object(None, f"{prefix}MaterialExpressionCustom_{index}")
    if node and "SW Kelvin Wake M7" in str(node.get_editor_property("description")):
        custom = node
        break
if not custom:
    raise RuntimeError("M7 Custom node is missing")

names = [str(value.get_editor_property("input_name"))
         for value in custom.get_editor_property("inputs")]
expected = ["WorldPosition", "ShipWakeTex", "ShipWakeGolden",
            "ShipWakeServerTime", "ShipWakeCount", "ShipWakeEnable"]
if names != expected:
    raise RuntimeError(f"Wrong Custom inputs: {names}")
if list(custom.get_editor_property("include_file_paths")) != ["/Project/SWShipWake.ush"]:
    raise RuntimeError("Wrong Custom include")
code = str(custom.get_editor_property("code"))
if "SW_M7_EVALUATE_KELVIN" not in code or "Height *" in code:
    raise RuntimeError("Wrong M7 WPO code or extra height multiplier")
inputs = list(unreal.MaterialEditingLibrary.get_inputs_for_material_expression(material, custom))
if len(inputs) != len(expected) or any(node is None for node in inputs):
    raise RuntimeError("M7 Custom input connection is incomplete")

ship_wake_parameters = []
for obj in unreal.get_objects_with_outer(material, include_nested_objects=True):
    if isinstance(obj, (unreal.MaterialExpressionScalarParameter,
                        unreal.MaterialExpressionVectorParameter,
                        unreal.MaterialExpressionTextureObjectParameter)):
        name = str(obj.get_editor_property("parameter_name"))
        if name.startswith("ShipWake"):
            ship_wake_parameters.append(name)
expected_parameters = sorted(expected[1:])
if sorted(ship_wake_parameters) != expected_parameters:
    raise RuntimeError(f"Obsolete or duplicate ShipWake parameters remain: {ship_wake_parameters}")

cdo = unreal.get_default_object(ship.generated_class())
emitter = cdo.get_editor_property("ship_wake_emitter")
if not emitter:
    raise RuntimeError("M7 emitter is missing")
unreal.log_warning(
    f"[KelvinM7Validation] PASS custom={custom.get_name()} inputs={names} "
    f"parameters={ship_wake_parameters} "
    f"spacing={emitter.get_editor_property('emission_distance_cm')} "
    f"turn={emitter.get_editor_property('maximum_turn_angle_degrees')}")
