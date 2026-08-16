import unreal


ROOT = "/Game/New/Water/Realistic_Water"
MATERIAL_PATH = f"{ROOT}/M_Realistic_Water"
SHIP_PATH = "/Game/Tests/Landscape/Kelvin/BP_PlayerShip_Kelvin"

WPO_CODE = """float Height = 0.0;
if (ShipWakeEnable > 0.5)
{
    SW_M7_EVALUATE_KELVIN(
        WorldPosition.xy,
        ShipWakeServerTime,
        ShipWakeCount,
        ShipWakeTex, ShipWakeTexSampler,
        ShipWakeGolden, ShipWakeGoldenSampler,
        Height);
}
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
        raise RuntimeError(f"Connection failed: {source.get_name()} -> {target_input}")


def create_scalar(material, name, default, x, y):
    node = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionScalarParameter, x, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("default_value", default)
    return node


def create_texture(material, name, default, x, y):
    node = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionTextureObjectParameter, x, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("texture", default)
    return node


material = load(MATERIAL_PATH)
prefix = f"{MATERIAL_PATH}.M_Realistic_Water:"
custom = None
parameter_nodes = []
for index in range(900):
    node = unreal.load_object(None, f"{prefix}MaterialExpressionCustom_{index}")
    if node:
        description = str(node.get_editor_property("description"))
        if "SW Kelvin Wake" in description:
            if node.get_editor_property("output_type") == unreal.CustomMaterialOutputType.CMOT_FLOAT3:
                custom = node
    for class_name in (
            "MaterialExpressionScalarParameter",
            "MaterialExpressionVectorParameter",
            "MaterialExpressionTextureObjectParameter"):
        parameter = unreal.load_object(None, f"{prefix}{class_name}_{index}")
        if parameter:
            try:
                name = str(parameter.get_editor_property("parameter_name"))
            except Exception:
                continue
            if name.startswith("ShipWake"):
                parameter_nodes.append(parameter)

if not custom:
    raise RuntimeError("Kelvin WPO Custom node was not found")

old_names = [str(value.get_editor_property("input_name"))
             for value in custom.get_editor_property("inputs")]
old_nodes = list(unreal.MaterialEditingLibrary.get_inputs_for_material_expression(material, custom))
old_inputs = {name: old_nodes[index] for index, name in enumerate(old_names)
              if index < len(old_nodes) and old_nodes[index]}
world_position = old_inputs.get("WorldPosition")
if not world_position:
    raise RuntimeError("Existing Kelvin node has no WorldPosition input")

black = unreal.load_asset("/Engine/EngineResources/Black")
if not black:
    black = unreal.load_asset("/Engine/EngineResources/DefaultTexture")


def find_parameter(name):
    for node in parameter_nodes:
        if str(node.get_editor_property("parameter_name")) == name:
            return node
    return None


event_tex = find_parameter("ShipWakeTex")
if event_tex:
    event_tex.set_editor_property("parameter_name", "ShipWakeTex")
    event_tex.set_editor_property("texture", black)
else:
    event_tex = create_texture(material, "ShipWakeTex", black, 11700, -1100)
golden_tex = find_parameter("ShipWakeGolden")
if golden_tex:
    golden_tex.set_editor_property("parameter_name", "ShipWakeGolden")
    golden_tex.set_editor_property("texture", black)
else:
    golden_tex = create_texture(material, "ShipWakeGolden", black, 11700, -900)
server_time = find_parameter("ShipWakeServerTime") or create_scalar(
    material, "ShipWakeServerTime", 0.0, 11700, -700)
wake_count = find_parameter("ShipWakeCount") or create_scalar(
    material, "ShipWakeCount", 0.0, 11700, -500)
wake_enable = find_parameter("ShipWakeEnable") or create_scalar(
    material, "ShipWakeEnable", 1.0, 11700, -300)

custom.modify()
custom.set_editor_property("description", "SW Kelvin Wake M7 Golden Event WPO")
custom.set_editor_property("include_file_paths", ["/Project/SWShipWake.ush"])
custom.set_editor_property("inputs", [
    custom_input("WorldPosition"), custom_input("ShipWakeTex"),
    custom_input("ShipWakeGolden"), custom_input("ShipWakeServerTime"),
    custom_input("ShipWakeCount"), custom_input("ShipWakeEnable")])
custom.set_editor_property("code", WPO_CODE)
for source, input_name in (
        (world_position, "WorldPosition"), (event_tex, "ShipWakeTex"),
        (golden_tex, "ShipWakeGolden"), (server_time, "ShipWakeServerTime"),
        (wake_count, "ShipWakeCount"), (wake_enable, "ShipWakeEnable")):
    connect(source, custom, input_name)

kept = {node.get_path_name() for node in (
    event_tex, golden_tex, server_time, wake_count, wake_enable)}
for node in parameter_nodes:
    if node.get_path_name() not in kept:
        unreal.MaterialEditingLibrary.delete_material_expression(material, node)

unreal.MaterialEditingLibrary.recompile_material(material)
if not unreal.EditorAssetLibrary.save_loaded_asset(material, only_if_is_dirty=False):
    raise RuntimeError(f"Failed to save {MATERIAL_PATH}")

ship = load(SHIP_PATH)
unreal.BlueprintEditorLibrary.compile_blueprint(ship)
cdo = unreal.get_default_object(ship.generated_class())
emitter = cdo.get_editor_property("ship_wake_emitter")
if not emitter:
    raise RuntimeError("ShipWakeEmitter is missing on BP_PlayerShip_Kelvin")
for name, value in {
        "kelvin_apex_local_offset": unreal.Vector(1500.0, 0.0, 0.0),
        "kelvin_direction_yaw_degrees": 0.0,
        "emission_distance_cm": 250.0,
        "maximum_turn_angle_degrees": 8.0,
        "maximum_emission_interval": 0.20,
        "maximum_catch_up_events": 8,
        "minimum_speed_cm_per_second": 250.0,
        "maximum_amplitude_cm": 65.0,
        "propagation_speed_cm_per_second": 1200.0,
        "decay_rate": 0.12,
        "wake_length_cm": 16000.0,
        "wake_half_width_cm": 6000.0,
        "envelope_width_cm": 2500.0,
        "fade_in_seconds": 0.08,
        "enable_client_prediction": True,
}.items():
    emitter.set_editor_property(name, value)
unreal.BlueprintEditorLibrary.compile_blueprint(ship)
if not unreal.EditorAssetLibrary.save_loaded_asset(ship, only_if_is_dirty=False):
    raise RuntimeError(f"Failed to save {SHIP_PATH}")

unreal.log_warning(
    f"[KelvinM7] PASS material={MATERIAL_PATH} custom={custom.get_name()} ship={SHIP_PATH}")
