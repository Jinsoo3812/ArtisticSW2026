import unreal


ROOT = "/Game/New/Water/Realistic_Water"
UPDATE_MATERIAL_PATH = f"{ROOT}/M_SWShipWakeFieldUpdate"
WATER_MATERIAL_PATH = f"{ROOT}/M_Realistic_Water"
SHIP_PATH = "/Game/Tests/Landscape/Kelvin/BP_PlayerShip_Kelvin"
LEVEL_PATH = f"{ROOT}/Realistic_Water"

M3_EMITTER_DEFAULTS = {
    # The visual field is integrated at a fixed simulation rate, so event capture
    # no longer needs to run at the render frame rate. These values are only for
    # updating the moving source pose and strengths.
    "minimum_emission_interval": 0.05,
    "emission_distance_cm": 50.0,
    "spectrum_speed_smoothing_rate": 2.5,
    "lifetime_seconds": 1.0,
}

UPDATE_CODE = """float Height;
SW_UPDATE_SHIP_WAKE_FIELD(
    UV,
    PreviousHeightState, PreviousHeightStateSampler,
    CurrentHeightState, CurrentHeightStateSampler,
    ShipWakeTex, ShipWakeTexSampler,
    ShipWakeServerTime, ShipWakeCount,
    PreviousStateCenter.xy, CurrentStateCenter.xy, OutputStateCenter.xy,
    FieldSizeCm, FieldResolution, SimulationDeltaTime,
    FieldWaveSpeed, FieldDamping, FieldSourceRate,
    Height);
return float3(Height, Height, Height);"""

WPO_CODE = """float Height;
SW_SAMPLE_SHIP_WAKE_FIELD(
    WorldPosition.xy,
    ShipWakeHeightField, ShipWakeHeightFieldSampler,
    ShipWakeFieldCenter.xy,
    ShipWakeFieldSizeCm,
    Height);
return float3(0.0, 0.0, Height * HeightScale * saturate(Enable));"""


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


def create_vector(material, name, default, x, y):
    node = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionVectorParameter, x, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("default_value", default)
    return node


def create_texture(material, name, default, x, y):
    node = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionTextureObjectParameter, x, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("texture", default)
    return node


def set_emitter_defaults(emitter, label):
    if not emitter:
        raise RuntimeError(f"ShipWakeEmitter is missing: {label}")
    emitter.modify()
    for property_name, value in M3_EMITTER_DEFAULTS.items():
        emitter.set_editor_property(property_name, value)
    unreal.log_warning(f"[KelvinM3] configured {label}: {emitter.get_path_name()}")


black_texture = unreal.load_asset("/Engine/EngineResources/Black")
if not black_texture:
    black_texture = unreal.load_asset("/Engine/EngineResources/DefaultTexture")

update_material = unreal.load_asset(UPDATE_MATERIAL_PATH)
if not update_material:
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    update_material = asset_tools.create_asset(
        "M_SWShipWakeFieldUpdate",
        ROOT,
        unreal.Material,
        unreal.MaterialFactoryNew())
    if not update_material:
        raise RuntimeError("Failed to create M_SWShipWakeFieldUpdate")

    update_material.set_editor_property("material_domain", unreal.MaterialDomain.MD_SURFACE)
    update_material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
    update_material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)

    uv = unreal.MaterialEditingLibrary.create_material_expression(
        update_material, unreal.MaterialExpressionTextureCoordinate, -1100, -900)
    previous_state = create_texture(update_material, "PreviousHeightState", black_texture, -1100, -700)
    current_state = create_texture(update_material, "CurrentHeightState", black_texture, -1100, -500)
    wake_texture = create_texture(update_material, "ShipWakeTex", black_texture, -1100, -300)
    server_time = create_scalar(update_material, "ShipWakeServerTime", 0.0, -1100, -100)
    wake_count = create_scalar(update_material, "ShipWakeCount", 0.0, -1100, 100)
    previous_center = create_vector(
        update_material, "PreviousStateCenter", unreal.LinearColor(0.0, 0.0, 0.0, 0.0), -800, -900)
    current_center = create_vector(
        update_material, "CurrentStateCenter", unreal.LinearColor(0.0, 0.0, 0.0, 0.0), -800, -700)
    output_center = create_vector(
        update_material, "OutputStateCenter", unreal.LinearColor(0.0, 0.0, 0.0, 0.0), -800, -500)
    field_size = create_scalar(update_material, "FieldSizeCm", 40000.0, -800, -300)
    field_resolution = create_scalar(update_material, "FieldResolution", 512.0, -800, -100)
    delta_time = create_scalar(update_material, "SimulationDeltaTime", 1.0 / 30.0, -800, 100)
    wave_speed = create_scalar(update_material, "FieldWaveSpeed", 650.0, -500, -500)
    damping = create_scalar(update_material, "FieldDamping", 0.85, -500, -300)
    source_rate = create_scalar(update_material, "FieldSourceRate", 2.0, -500, -100)

    custom = unreal.MaterialEditingLibrary.create_material_expression(
        update_material, unreal.MaterialExpressionCustom, -100, -450)
    custom.set_editor_property("description", "SW Ship Wake M3 Signed Height Update")
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    custom.set_editor_property("include_file_paths", ["/Project/SWShipWakeField.ush"])
    custom.set_editor_property("inputs", [
        custom_input("UV"),
        custom_input("PreviousHeightState"),
        custom_input("CurrentHeightState"),
        custom_input("ShipWakeTex"),
        custom_input("ShipWakeServerTime"),
        custom_input("ShipWakeCount"),
        custom_input("PreviousStateCenter"),
        custom_input("CurrentStateCenter"),
        custom_input("OutputStateCenter"),
        custom_input("FieldSizeCm"),
        custom_input("FieldResolution"),
        custom_input("SimulationDeltaTime"),
        custom_input("FieldWaveSpeed"),
        custom_input("FieldDamping"),
        custom_input("FieldSourceRate"),
    ])
    custom.set_editor_property("code", UPDATE_CODE)

    for source, input_name in (
        (uv, "UV"),
        (previous_state, "PreviousHeightState"),
        (current_state, "CurrentHeightState"),
        (wake_texture, "ShipWakeTex"),
        (server_time, "ShipWakeServerTime"),
        (wake_count, "ShipWakeCount"),
        (previous_center, "PreviousStateCenter"),
        (current_center, "CurrentStateCenter"),
        (output_center, "OutputStateCenter"),
        (field_size, "FieldSizeCm"),
        (field_resolution, "FieldResolution"),
        (delta_time, "SimulationDeltaTime"),
        (wave_speed, "FieldWaveSpeed"),
        (damping, "FieldDamping"),
        (source_rate, "FieldSourceRate"),
    ):
        connect(source, custom, input_name)

    if not unreal.MaterialEditingLibrary.connect_material_property(
            custom, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR):
        raise RuntimeError("Failed to connect M3 update Custom node to Emissive Color")
else:
    unreal.log_warning("[KelvinM3] update material already exists; preserving its graph")

unreal.MaterialEditingLibrary.recompile_material(update_material)
if not unreal.EditorAssetLibrary.save_loaded_asset(update_material, only_if_is_dirty=False):
    raise RuntimeError(f"Failed to save: {UPDATE_MATERIAL_PATH}")

water_material = load(WATER_MATERIAL_PATH)
asset_name = WATER_MATERIAL_PATH.rsplit("/", 1)[-1]
prefix = f"{WATER_MATERIAL_PATH}.{asset_name}:"
wpo_custom = None
for index in range(200):
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

old_input_names = [
    str(value.get_editor_property("input_name"))
    for value in wpo_custom.get_editor_property("inputs")
]
old_input_nodes = list(
    unreal.MaterialEditingLibrary.get_inputs_for_material_expression(water_material, wpo_custom))
old_inputs = {
    name: old_input_nodes[index]
    for index, name in enumerate(old_input_names)
    if index < len(old_input_nodes) and old_input_nodes[index]
}
for required in ("WorldPosition", "HeightScale", "Enable"):
    if required not in old_inputs:
        raise RuntimeError(f"Existing WPO input is missing: {required}")

height_field_node = old_inputs.get("ShipWakeTex")
if not height_field_node:
    height_field_node = create_texture(water_material, "ShipWakeHeightField", black_texture, 11800, -1250)
else:
    height_field_node.set_editor_property("parameter_name", "ShipWakeHeightField")
    height_field_node.set_editor_property("texture", black_texture)

field_center_node = create_vector(
    water_material,
    "ShipWakeFieldCenter",
    unreal.LinearColor(0.0, 0.0, 0.0, 0.0),
    11800,
    -1000)
field_size_node = create_scalar(
    water_material,
    "ShipWakeFieldSizeCm",
    40000.0,
    11800,
    -800)

wpo_custom.modify()
wpo_custom.set_editor_property("description", "SW Kelvin Wake M3 Signed Height Field WPO")
wpo_custom.set_editor_property("include_file_paths", ["/Project/SWShipWakeField.ush"])
wpo_custom.set_editor_property("inputs", [
    custom_input("WorldPosition"),
    custom_input("ShipWakeHeightField"),
    custom_input("ShipWakeFieldCenter"),
    custom_input("ShipWakeFieldSizeCm"),
    custom_input("HeightScale"),
    custom_input("Enable"),
])
wpo_custom.set_editor_property("code", WPO_CODE)

for source, input_name in (
    (old_inputs["WorldPosition"], "WorldPosition"),
    (height_field_node, "ShipWakeHeightField"),
    (field_center_node, "ShipWakeFieldCenter"),
    (field_size_node, "ShipWakeFieldSizeCm"),
    (old_inputs["HeightScale"], "HeightScale"),
    (old_inputs["Enable"], "Enable"),
):
    connect(source, wpo_custom, input_name)

unreal.MaterialEditingLibrary.recompile_material(water_material)
if not unreal.EditorAssetLibrary.save_loaded_asset(water_material, only_if_is_dirty=False):
    raise RuntimeError(f"Failed to save: {WATER_MATERIAL_PATH}")

ship = load(SHIP_PATH)
unreal.BlueprintEditorLibrary.compile_blueprint(ship)
ship_cdo = unreal.get_default_object(ship.generated_class())
set_emitter_defaults(ship_cdo.get_editor_property("ship_wake_emitter"), "Blueprint CDO")
unreal.BlueprintEditorLibrary.compile_blueprint(ship)
if not unreal.EditorAssetLibrary.save_loaded_asset(ship, only_if_is_dirty=False):
    raise RuntimeError(f"Failed to save ship Blueprint: {SHIP_PATH}")

world = unreal.EditorLoadingAndSavingUtils.load_map(LEVEL_PATH)
if not world:
    raise RuntimeError(f"Failed to load level: {LEVEL_PATH}")

placed_count = 0
for actor in unreal.EditorLevelLibrary.get_all_level_actors():
    if "BP_PlayerShip_Kelvin" not in actor.get_class().get_name() and \
            "BP_PlayerShip_Kelvin" not in actor.get_name():
        continue
    emitter = actor.get_component_by_class(unreal.SWShipWakeEmitterComponent)
    set_emitter_defaults(emitter, f"placed actor {actor.get_name()}")
    actor.modify()
    placed_count += 1

if placed_count and not unreal.EditorAssetLibrary.save_asset(
        LEVEL_PATH, only_if_is_dirty=False):
    raise RuntimeError(f"Failed to save level: {LEVEL_PATH}")

unreal.log_warning(
    "[KelvinM3] PASS "
    f"update_material={UPDATE_MATERIAL_PATH} "
    f"water_material={WATER_MATERIAL_PATH} wpo={wpo_custom.get_name()} "
    f"ship={SHIP_PATH} placed_ships={placed_count}")
