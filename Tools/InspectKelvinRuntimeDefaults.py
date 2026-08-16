import unreal


SHIP_PATH = "/Game/Tests/Landscape/Kelvin/BP_PlayerShip_Kelvin"
LEVEL_PATH = "/Game/New/Water/Realistic_Water/Realistic_Water"


def describe(label, emitter):
    if not emitter:
        unreal.log_warning(f"[KelvinRuntimeDefaults] {label} emitter=MISSING")
        return
    names = [
        "maximum_amplitude_cm",
        "minimum_emission_interval",
        "emission_distance_cm",
        "minimum_speed_cm_per_second",
        "spectrum_speed_smoothing_rate",
        "lifetime_seconds",
        "pressure_size_cm",
    ]
    values = " ".join(
        f"{name}={emitter.get_editor_property(name)}" for name in names)
    unreal.log_warning(
        f"[KelvinRuntimeDefaults] {label} path={emitter.get_path_name()} {values}")


ship = unreal.load_asset(SHIP_PATH)
if not ship:
    raise RuntimeError(f"Missing Blueprint: {SHIP_PATH}")
unreal.BlueprintEditorLibrary.compile_blueprint(ship)
cdo = unreal.get_default_object(ship.generated_class())
describe("BlueprintCDO", cdo.get_editor_property("ship_wake_emitter"))

world = unreal.EditorLoadingAndSavingUtils.load_map(LEVEL_PATH)
if not world:
    raise RuntimeError(f"Missing level: {LEVEL_PATH}")
found = 0
for actor in unreal.EditorLevelLibrary.get_all_level_actors():
    if "BP_PlayerShip_Kelvin" not in actor.get_class().get_name() and \
            "BP_PlayerShip_Kelvin" not in actor.get_name():
        continue
    describe(
        f"PlacedActor:{actor.get_name()}",
        actor.get_component_by_class(unreal.SWShipWakeEmitterComponent))
    found += 1

unreal.log_warning(f"[KelvinRuntimeDefaults] PASS placed_actor_count={found}")
