import unreal

registry = unreal.AssetRegistryHelpers.get_asset_registry()

def log(value):
    unreal.log_warning("FUSE_INSPECT " + str(value))

def refs(package_name):
    options = unreal.AssetRegistryDependencyOptions(
        include_soft_package_references=True,
        include_hard_package_references=True,
        include_searchable_names=True,
        include_soft_management_references=True,
        include_hard_management_references=True)
    return [str(x) for x in registry.get_referencers(package_name, options)]

for folder in ("/Game/EditorBlueprintResources", "/Game/EngineSky"):
    assets = registry.get_assets_by_path(folder, recursive=True)
    log(folder + " assets=" + str(len(assets)))
    for data in assets:
        package = str(data.package_name)
        log("  " + package + " class=" + str(data.asset_class_path.asset_name) + " refs=" + str(refs(package)))

particle_path = "/Game/Pack_Simple_Particle_Burst/02_Blueprints/BP_Particle_Burst_Lvl_1"
particle_bp = unreal.EditorAssetLibrary.load_asset(particle_path)
log("PARTICLE_BP=" + str(particle_bp))
if particle_bp:
    log("PARTICLE_REFS=" + str(refs(particle_path)))
    cdo = unreal.get_default_object(particle_bp.generated_class())
    log("PARTICLE_CLASS=" + cdo.get_class().get_name())
    for component in cdo.get_components_by_class(unreal.SceneComponent):
        info = "  PARTICLE_COMP name={} class={} rel={} scale={}".format(
            component.get_name(), component.get_class().get_name(),
            component.get_editor_property("relative_location"),
            component.get_editor_property("relative_scale3d"))
        if isinstance(component, unreal.NiagaraComponent):
            info += " asset={} auto_activate={}".format(
                component.get_editor_property("asset"), component.get_editor_property("auto_activate"))
        log(info)
    log("PARTICLE_DEPENDENCIES=" + str([str(x) for x in registry.get_dependencies(particle_path, unreal.AssetRegistryDependencyOptions(
        include_soft_package_references=True, include_hard_package_references=True,
        include_searchable_names=True, include_soft_management_references=True,
        include_hard_management_references=True))]))
    log("PARTICLE_INITIAL_LIFESPAN=" + str(cdo.get_editor_property("initial_life_span")))

mesh = unreal.EditorAssetLibrary.load_asset("/Game/Blueprints/Ship/Enemy_Ship/Torpeodo/SM_Bomba")
if mesh:
    try:
        sockets = mesh.get_editor_property("sockets")
        log("SM_BOMBA_SOCKETS=" + str([(s.get_name(), s.get_editor_property("relative_location"), s.get_editor_property("relative_rotation")) for s in sockets]))
    except Exception as exc:
        log("SM_BOMBA_SOCKET_READ_ERROR=" + str(exc))
    log("SM_METHODS=" + str([name for name in dir(mesh) if "socket" in name.lower()]))
    probe = unreal.EditorLevelLibrary.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(), unreal.Rotator())
    probe.static_mesh_component.set_static_mesh(mesh)
    log("SM_BOMBA_SOCKET_NAMES=" + str([str(x) for x in probe.static_mesh_component.get_all_socket_names()]))
    unreal.EditorLevelLibrary.destroy_actor(probe)

showcase = unreal.EditorLoadingAndSavingUtils.load_map("/Game/Pack_Simple_Particle_Burst/04_Maps/SImple_Particle_Burst_Showcased")
if particle_bp:
    for actor in unreal.EditorLevelLibrary.get_all_level_actors():
        if actor.get_class() == particle_bp.generated_class():
            log("PARTICLE_INSTANCE=" + actor.get_name())
            for component in actor.get_components_by_class(unreal.ActorComponent):
                info = "  INSTANCE_COMP name={} class={}".format(component.get_name(), component.get_class().get_name())
                if isinstance(component, unreal.NiagaraComponent):
                    info += " asset={} auto_activate={} auto_destroy={}".format(
                        component.get_editor_property("asset"), component.get_editor_property("auto_activate"),
                        component.get_editor_property("auto_destroy"))
                log(info)
            break

torpedo_bp = unreal.EditorAssetLibrary.load_asset("/Game/Blueprints/Ship/Enemy_Ship/Blueprints/BP_ES_Torpedo")
if torpedo_bp:
    cdo = unreal.get_default_object(torpedo_bp.generated_class())
    log("TORPEDO_CLASS=" + cdo.get_class().get_name())
    for component in cdo.get_components_by_class(unreal.SceneComponent):
        if isinstance(component, unreal.StaticMeshComponent):
            log("  TORPEDO_MESH_COMP name={} mesh={} scale={} overlay={}".format(
                component.get_name(), component.get_editor_property("static_mesh"),
                component.get_editor_property("relative_scale3d"), component.get_editor_property("overlay_material")))
    log("TORPEDO_PULSE_PROPERTY=" + str(cdo.get_editor_property("pulse_overlay_material")))
