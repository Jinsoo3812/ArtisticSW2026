import unreal


MATERIAL_PATH = "/Game/New/Water/Realistic_Water/M_Realistic_Water"
SHIP_PATH = "/Game/Tests/Landscape/Kelvin/BP_PlayerShip_Kelvin"
LEVEL_PATH = "/Game/New/Water/Realistic_Water/Realistic_Water"

WPO_CODE = """float Height;
SW_EVALUATE_SHIP_WAKE_M2(WorldPosition.xy, ShipWakeServerTime, ShipWakeCount, ShipWakeTex, ShipWakeTexSampler, Height);
return float3(0.0, 0.0, Height * HeightScale * saturate(Enable));"""

M2_DEFAULTS = {
    "hull_length_cm": 2400.0,
    "beam_width_cm": 600.0,
    "draft_cm": 250.0,
    "stern_offset_cm": 900.0,
    "minimum_emission_interval": 0.05,
    "emission_distance_cm": 50.0,
    "minimum_speed_cm_per_second": 250.0,
    "maximum_amplitude_cm": 65.0,
    "lifetime_seconds": 1.0,
    "wake_length_multiplier": 8.0,
    "transverse_strength": 0.55,
    "divergent_strength": 1.0,
    "stern_strength": 0.72,
    "stern_phase_offset_radians": 2.15,
}


def load(path):
    asset = unreal.load_asset(path)
    if not asset:
        raise RuntimeError(f"Required asset is missing: {path}")
    return asset


def set_emitter_defaults(emitter, label):
    if not emitter:
        raise RuntimeError(f"ShipWakeEmitter is missing: {label}")
    emitter.modify()
    for property_name, value in M2_DEFAULTS.items():
        emitter.set_editor_property(property_name, value)
    unreal.log_warning(f"[KelvinM2] configured {label}: {emitter.get_path_name()}")


material = load(MATERIAL_PATH)
asset_name = MATERIAL_PATH.rsplit("/", 1)[-1]
prefix = f"{MATERIAL_PATH}.{asset_name}:"
wpo_custom = None
for index in range(160):
    expression = unreal.load_object(None, f"{prefix}MaterialExpressionCustom_{index}")
    if not expression:
        continue
    description = str(expression.get_editor_property("description"))
    input_names = [
        str(value.get_editor_property("input_name"))
        for value in expression.get_editor_property("inputs")
    ]
    if "SW Kelvin Wake WPO" in description or (
            "ShipWakeTex" in input_names and expression.get_editor_property("output_type")
            == unreal.CustomMaterialOutputType.CMOT_FLOAT3):
        wpo_custom = expression
        break

if not wpo_custom:
    raise RuntimeError("SW Kelvin Wake WPO Custom expression was not found")

expected_inputs = [
    "WorldPosition",
    "ShipWakeTex",
    "ShipWakeServerTime",
    "ShipWakeCount",
    "HeightScale",
    "Enable",
]
actual_inputs = [
    str(value.get_editor_property("input_name"))
    for value in wpo_custom.get_editor_property("inputs")
]
if actual_inputs != expected_inputs:
    raise RuntimeError(
        f"Unexpected M2 WPO inputs. expected={expected_inputs} actual={actual_inputs}")

wpo_custom.modify()
wpo_custom.set_editor_property("description", "SW Kelvin Wake M2 WPO (directional spectrum)")
wpo_custom.set_editor_property("include_file_paths", ["/Project/SWShipWake.ush"])
wpo_custom.set_editor_property("code", WPO_CODE)
unreal.MaterialEditingLibrary.recompile_material(material)
if not unreal.EditorAssetLibrary.save_loaded_asset(material, only_if_is_dirty=False):
    raise RuntimeError(f"Failed to save material: {MATERIAL_PATH}")

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
    if "BP_PlayerShip_Kelvin" not in actor.get_class().get_name() and "BP_PlayerShip_Kelvin" not in actor.get_name():
        continue
    emitter = actor.get_component_by_class(unreal.SWShipWakeEmitterComponent)
    set_emitter_defaults(emitter, f"placed actor {actor.get_name()}")
    actor.modify()
    placed_count += 1

if placed_count:
    if not unreal.EditorAssetLibrary.save_asset(LEVEL_PATH, only_if_is_dirty=False):
        raise RuntimeError(f"Failed to save level: {LEVEL_PATH}")

unreal.log_warning(
    "[KelvinM2] PASS "
    f"material={MATERIAL_PATH} custom={wpo_custom.get_name()} "
    f"ship={SHIP_PATH} placed_ships={placed_count}")
