"""Report likely standalone ship meshes in Test_Level for cabin-volume baking."""
import traceback

import unreal


MAP_PATH = "/Game/Level/Test_Level"
def vector(value):
    return [value.x, value.y, value.z]


def main():
    unreal.EditorLoadingAndSavingUtils.load_map(MAP_PATH)
    subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    actors = subsystem.get_all_level_actors()
    result = []

    for actor in actors:
        components = actor.get_components_by_class(unreal.StaticMeshComponent)
        for component in components:
            mesh = component.get_editor_property("static_mesh")
            if mesh is None:
                continue
            label = actor.get_actor_label()
            mesh_path = mesh.get_path_name()
            if "ship" not in (label + " " + mesh_path).lower():
                continue
            origin, extent = actor.get_actor_bounds(False)
            body_setup = mesh.get_editor_property("body_setup")
            agg = body_setup.get_editor_property("agg_geom") if body_setup else None
            result.append({
                "actor_label": label,
                "actor_path": actor.get_path_name(),
                "actor_class": actor.get_class().get_name(),
                "tags": [str(tag) for tag in actor.tags],
                "mesh": mesh_path,
                "location": vector(actor.get_actor_location()),
                "rotation": [
                    actor.get_actor_rotation().roll,
                    actor.get_actor_rotation().pitch,
                    actor.get_actor_rotation().yaw,
                ],
                "scale": vector(actor.get_actor_scale3d()),
                "bounds_origin": vector(origin),
                "bounds_extent": vector(extent),
                "collision_trace_flag": str(
                    body_setup.get_editor_property("collision_trace_flag"))
                    if body_setup else "None",
                "simple_collision": str(agg) if agg else "None",
                "visible": component.is_visible(),
                "collision_enabled": str(component.get_collision_enabled()),
            })

    unreal.log("SW_SHIP_SCAN_TARGET_COUNT={}".format(len(result)))
    for item in result:
        unreal.log("SW_SHIP_SCAN_TARGET={} | {} | tags={} | location={} | extent={}".format(
            item["actor_label"], item["mesh"], item["tags"],
            item["location"], item["bounds_extent"]))

    for actor in actors:
        label = actor.get_actor_label()
        if (label.startswith("Cube") or isinstance(actor, unreal.PointLight)):
            unreal.log("SW_SHIP_SCAN_AUX={} | class={} | tags={} | location={}".format(
                label, actor.get_class().get_name(),
                [str(tag) for tag in actor.tags],
                vector(actor.get_actor_location())))

    for actor in actors:
        if actor.get_actor_label() == "SW_CabinVolume_Debug":
            components = actor.get_components_by_class(unreal.InstancedStaticMeshComponent)
            instance_count = components[0].get_instance_count() if components else 0
            unreal.log("SW_CABIN_DEBUG_SAVED=TRUE | tags={} | instances={}".format(
                [str(tag) for tag in actor.tags], instance_count))


try:
    main()
except Exception:
    unreal.log_error(traceback.format_exc())
    raise
