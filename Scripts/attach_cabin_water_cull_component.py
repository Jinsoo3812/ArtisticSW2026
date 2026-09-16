import unreal


TARGET_PATH = "/Game/Tests/Landscape/Kelvin/BP_PlayerShip_Kelvin"


def find_target_blueprint():
    blueprint = unreal.load_asset(TARGET_PATH)
    if not blueprint:
        raise RuntimeError(f"Could not load {TARGET_PATH}")
    return blueprint


blueprint = find_target_blueprint()
if not unreal.RealisticWaterMaterialPipelineLibrary.add_cabin_water_cull_component_to_blueprint(blueprint):
    raise RuntimeError("Failed to add SWCabinWaterCullComponent")

if not unreal.EditorAssetLibrary.save_loaded_asset(blueprint, False):
    raise RuntimeError("Failed to save target Blueprint")

unreal.EditorLoadingAndSavingUtils.load_map("/Game/Level/Test_Level")
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
targets = [a for a in actors if a.get_actor_label() == "BP_PlayerShip_Kelvin"]
if len(targets) != 1:
    raise RuntimeError(f"Expected one Kelvin actor in Test_Level, got {len(targets)}")
components = targets[0].get_components_by_class(unreal.SWCabinWaterCullComponent)
if len(components) != 1:
    raise RuntimeError(f"Expected one live inherited cabin-cull component, got {len(components)}")

unreal.log(f"[CabinCull] Attached and verified on {blueprint.get_path_name()}: {components[0].get_name()}")
