import traceback

import unreal


LEVEL = "/Game/Level/Mesh_Test"
SHIP_LABEL = "SM_Ship_Visual"
SEED_LABEL = "Point_Light_Culling"
BARRIER_FOLDER = "Barrier"
MPC_PATH = "/Game/Blueprints/Water/MPC_Water_Custom"
DATA_PATH = "/Game/Blueprints/Water/Culling/DA_SW_ShipCabinWaterCull"
BLUEPRINT_PATHS = (
    "/Game/Blueprints/Ship/Blueprints/BP_PlayerShip_Kelvin",
    "/Game/Blueprints/Ship/Enemy_Ship/Blueprints/BP_EnemyShip",
)


def set_tag(actor, tag, enabled):
    tags = list(actor.get_editor_property("tags"))
    tag_name = unreal.Name(tag)
    has_tag = any(str(value) == tag for value in tags)
    if enabled and not has_tag:
        tags.append(tag_name)
    elif not enabled and has_tag:
        tags = [value for value in tags if str(value) != tag]
    else:
        return
    actor.set_editor_property("tags", tags)


def prepare_bake_actors():
    actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    actors = list(actor_subsystem.get_all_level_actors())
    ships = [actor for actor in actors if actor.get_actor_label() == SHIP_LABEL]
    normalized_seed_label = SEED_LABEL.replace("_", "").replace(" ", "").lower()
    seeds = [
        actor for actor in actors
        if actor.get_actor_label().replace("_", "").replace(" ", "").lower()
        == normalized_seed_label
    ]
    if len(ships) != 1:
        raise RuntimeError("Expected one {}, got {}".format(SHIP_LABEL, len(ships)))
    if not isinstance(ships[0], unreal.StaticMeshActor):
        raise RuntimeError("{} must be a StaticMeshActor".format(SHIP_LABEL))
    if len(seeds) != 1:
        candidates = [
            "{} ({})".format(actor.get_actor_label(), actor.get_class().get_name())
            for actor in actors
            if isinstance(actor, unreal.PointLight)
            or "cull" in actor.get_actor_label().lower()
            or "seed" in actor.get_actor_label().lower()
        ]
        unreal.log_error("SW_CABIN_SEED_CANDIDATES {}".format(candidates))
        raise RuntimeError("Expected one {}, got {}".format(SEED_LABEL, len(seeds)))
    if not isinstance(seeds[0], unreal.PointLight):
        raise RuntimeError("{} must be a PointLight".format(SEED_LABEL))

    barriers = []
    for actor in actors:
        folder = str(actor.get_folder_path()).replace("\\", "/")
        in_barrier_folder = BARRIER_FOLDER.lower() in [
            part.lower() for part in folder.split("/") if part
        ]
        is_barrier = (
            in_barrier_folder
            and isinstance(actor, unreal.StaticMeshActor)
        )
        set_tag(actor, "SW_CabinBarrier", is_barrier)
        if is_barrier:
            barriers.append(actor)

        set_tag(actor, "SW_CabinShip", actor == ships[0])
        set_tag(actor, "SW_CabinSeed", actor == seeds[0])

    if not barriers:
        candidates = [
            "{} folder={} class={}".format(
                actor.get_actor_label(), actor.get_folder_path(), actor.get_class().get_name())
            for actor in actors
            if "cube" in actor.get_actor_label().lower()
            or "barrier" in str(actor.get_folder_path()).lower()
        ]
        unreal.log_error("SW_CABIN_BARRIER_CANDIDATES {}".format(candidates))
        raise RuntimeError("No StaticMeshActors were found in the Barrier folder")

    ship_component = ships[0].static_mesh_component
    mesh = ship_component.get_editor_property("static_mesh")
    unreal.log_warning(
        "SW_CABIN_INPUT ship={} mesh={} barriers={} seed={} transform={}".format(
            ships[0].get_path_name(),
            mesh.get_path_name() if mesh else "None",
            len(barriers),
            seeds[0].get_path_name(),
            ships[0].get_actor_transform(),
        )
    )


def update_runtime_assets():
    data = unreal.load_asset(DATA_PATH)
    mpc = unreal.load_asset(MPC_PATH)
    if data is None or mpc is None:
        raise RuntimeError("Cabin cull Data Asset or MPC could not be loaded")

    helper = unreal.RealisticWaterMaterialPipelineLibrary
    if not helper.configure_cabin_water_cull_collection(mpc):
        raise RuntimeError("Could not configure cabin water-cull MPC parameters")
    if not helper.set_cabin_water_cull_bounds_defaults(
            mpc,
            data.get_editor_property("local_bounds_min"),
            data.get_editor_property("local_bounds_max")):
        raise RuntimeError("Could not update cabin water-cull MPC bounds")
    if not unreal.EditorAssetLibrary.save_loaded_asset(mpc, only_if_is_dirty=False):
        raise RuntimeError("Could not save cabin water-cull MPC")

    for path in BLUEPRINT_PATHS:
        blueprint = unreal.load_asset(path)
        if blueprint is None:
            raise RuntimeError("Could not load {}".format(path))
        if not helper.add_cabin_water_cull_component_to_blueprint(blueprint):
            raise RuntimeError("Could not add/verify CabinWaterCull on {}".format(path))
        unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
        if not unreal.EditorAssetLibrary.save_loaded_asset(
                blueprint, only_if_is_dirty=False):
            raise RuntimeError("Could not save {}".format(path))
        unreal.log_warning("SW_CABIN_BLUEPRINT_UPDATED path={}".format(path))

    unreal.log_warning(
        "SW_CABIN_BOUNDS min={} max={} resolution={} voxel_size={}".format(
            data.get_editor_property("local_bounds_min"),
            data.get_editor_property("local_bounds_max"),
            data.get_editor_property("resolution"),
            data.get_editor_property("voxel_size_cm"),
        )
    )


def main():
    unreal.EditorLoadingAndSavingUtils.load_map(LEVEL)
    prepare_bake_actors()
    result = None
    selected_thickness = None
    for surface_thickness in (12.0, 18.0, 25.0, 35.0):
        result = unreal.ShipCabinVolumeBakerLibrary.bake_tagged_cabin_debug(
            voxel_size=10.0,
            surface_thickness=surface_thickness,
            bounds_padding=30.0,
        )
        unreal.log_warning(
            "SW_CABIN_BAKE_ATTEMPT thickness={} success={} leaked={} filled={} "
            "instances={} message={}".format(
                surface_thickness,
                result.success,
                result.leaked_to_exterior,
                result.filled_voxel_count,
                result.debug_instance_count,
                result.message,
            )
        )
        if result.success:
            selected_thickness = surface_thickness
            break
    if not result.success:
        raise RuntimeError(result.message)
    if not unreal.EditorAssetLibrary.save_directory(
            "/Game/Blueprints/Water/Culling",
            only_if_is_dirty=False,
            recursive=True):
        raise RuntimeError("Cabin bake assets could not be saved")
    update_runtime_assets()
    if not unreal.EditorLoadingAndSavingUtils.save_current_level():
        raise RuntimeError("Mesh_Test could not be saved")
    unreal.log_warning(
        "SW_CABIN_PIPELINE=PASS surface_thickness={}".format(selected_thickness))


try:
    main()
except Exception:
    unreal.log_error(traceback.format_exc())
    raise
