import unreal


ROOT = "/Game/New/Water/Realistic_Water"
WATER_MATERIAL_PATH = f"{ROOT}/M_Realistic_Water"
SHIP_PATH = "/Game/Tests/Landscape/Kelvin/BP_PlayerShip_Kelvin"

WPO_CODE = """float Height;
SW_M4_EVALUATE_KELVIN(
    WorldPosition.xy,
    ShipWakeServerTime,
    ShipWakeCount,
    ShipWakeTex, ShipWakeTexSampler,
    ShipWakeTrajectoryTex, ShipWakeTrajectoryTexSampler,
    ShipWakeAtlas, ShipWakeAtlasSampler,
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


def connect(source, target, target_input, source_output=""):
    if not unreal.MaterialEditingLibrary.connect_material_expressions(
            source, source_output, target, target_input):
        raise RuntimeError(
            f"Connection failed: {source.get_name()}[{source_output}] -> "
            f"{target.get_name()}[{target_input}]")


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


material = load(WATER_MATERIAL_PATH)
asset_name = WATER_MATERIAL_PATH.rsplit("/", 1)[-1]
prefix = f"{WATER_MATERIAL_PATH}.{asset_name}:"
wpo_custom = None
for index in range(260):
    expression = unreal.load_object(None, f"{prefix}MaterialExpressionCustom_{index}")
    if not expression:
        continue
    description = str(expression.get_editor_property("description"))
    if "SW Kelvin Wake" in description and expression.get_editor_property(
            "output_type") == unreal.CustomMaterialOutputType.CMOT_FLOAT3:
        wpo_custom = expression
        break
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
for required in ("WorldPosition",):
    if required not in old_inputs:
        raise RuntimeError(f"Existing WPO input is missing: {required}")

black = unreal.load_asset("/Engine/EngineResources/Black")
if not black:
    black = unreal.load_asset("/Engine/EngineResources/DefaultTexture")

# Reuse the former M3 field object as the compact event-state texture. The
# orphaned M3 update material is deliberately left intact as a debug artifact.
wake_texture = old_inputs.get("ShipWakeTex") or old_inputs.get("ShipWakeHeightField")
if wake_texture:
    wake_texture.set_editor_property("parameter_name", "ShipWakeTex")
    wake_texture.set_editor_property("texture", black)
else:
    wake_texture = create_texture(material, "ShipWakeTex", black, 11700, -1300)
trajectory_texture = old_inputs.get("ShipWakeTrajectoryTex") or create_texture(
    material, "ShipWakeTrajectoryTex", black, 11700, -1100)
atlas_texture = old_inputs.get("ShipWakeAtlas") or create_texture(
    material, "ShipWakeAtlas", black, 11700, -900)
server_time = old_inputs.get("ShipWakeServerTime") or create_scalar(
    material, "ShipWakeServerTime", 0.0, 11700, -700)
wake_count = old_inputs.get("ShipWakeCount") or create_scalar(
    material, "ShipWakeCount", 0.0, 11700, -500)

wpo_custom.modify()
wpo_custom.set_editor_property("description", "SW Kelvin Wake M4 Golden Atlas WPO")
wpo_custom.set_editor_property("include_file_paths", ["/Project/SWShipWake.ush"])
wpo_custom.set_editor_property("inputs", [
    custom_input("WorldPosition"),
    custom_input("ShipWakeTex"),
    custom_input("ShipWakeTrajectoryTex"),
    custom_input("ShipWakeAtlas"),
    custom_input("ShipWakeServerTime"),
    custom_input("ShipWakeCount"),
])
wpo_custom.set_editor_property("code", WPO_CODE)

for source, input_name in (
    (old_inputs["WorldPosition"], "WorldPosition"),
    (wake_texture, "ShipWakeTex"),
    (trajectory_texture, "ShipWakeTrajectoryTex"),
    (atlas_texture, "ShipWakeAtlas"),
    (server_time, "ShipWakeServerTime"),
    (wake_count, "ShipWakeCount"),
):
    connect(source, wpo_custom, input_name)

unreal.MaterialEditingLibrary.recompile_material(material)
if not unreal.EditorAssetLibrary.save_loaded_asset(material, only_if_is_dirty=False):
    raise RuntimeError(f"Failed to save: {WATER_MATERIAL_PATH}")

ship = load(SHIP_PATH)
unreal.BlueprintEditorLibrary.compile_blueprint(ship)
cdo = unreal.get_default_object(ship.generated_class())
emitter = cdo.get_editor_property("ship_wake_emitter")
if not emitter:
    raise RuntimeError("ShipWakeEmitter is missing on BP_PlayerShip_Kelvin")
emitter.modify()
for name, value in {
    "kelvin_apex_local_offset": unreal.Vector(1500.0, 0.0, 0.0),
    "kelvin_direction_yaw_degrees": 0.0,
    "pressure_size_cm": 2400.0,
    "longitudinal_scale": 1.0,
    "lateral_scale": 1.0,
    "near_hull_suppress_distance_cm": 0.0,
    "trajectory_sample_distance_cm": 500.0,
    "minimum_emission_interval": 0.05,
    "emission_distance_cm": 50.0,
    "spectrum_speed_smoothing_rate": 2.5,
    "lifetime_seconds": 1.0,
}.items():
    emitter.set_editor_property(name, value)
unreal.BlueprintEditorLibrary.compile_blueprint(ship)
if not unreal.EditorAssetLibrary.save_loaded_asset(ship, only_if_is_dirty=False):
    raise RuntimeError(f"Failed to save ship Blueprint: {SHIP_PATH}")

unreal.log_warning(
    "[KelvinM4] PASS "
    f"material={WATER_MATERIAL_PATH} custom={wpo_custom.get_name()} ship={SHIP_PATH}")
