"""Report the exact tagged cull target and ship-mesh component transforms in Test_Level."""
import unreal


LEVEL = "/Game/Level/Test_Level"


def vec(value):
    return [round(value.x, 3), round(value.y, 3), round(value.z, 3)]


def rot(value):
    return [round(value.roll, 3), round(value.pitch, 3), round(value.yaw, 3)]


def main():
    unreal.EditorLoadingAndSavingUtils.load_map(LEVEL)
    subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    actors = subsystem.get_all_level_actors()
    targets = []
    for actor in actors:
        tags = [str(tag) for tag in actor.tags]
        label = actor.get_actor_label()
        if "SW_CabinCullTarget" in tags:
            targets.append(label)
        if ("kelvin" not in label.lower() and label != "SM_Ship"
                and "SW_CabinCullTarget" not in tags):
            continue
        unreal.log("SW_CULL_DIAG_ACTOR label={} class={} tags={} loc={} rot={} scale={}".format(
            label, actor.get_class().get_name(), tags,
            vec(actor.get_actor_location()), rot(actor.get_actor_rotation()),
            vec(actor.get_actor_scale3d())))
        for component in actor.get_components_by_class(unreal.StaticMeshComponent):
            mesh = component.get_editor_property("static_mesh")
            if mesh is None or "sm_ship" not in mesh.get_path_name().lower():
                continue
            transform = component.get_world_transform()
            unreal.log("SW_CULL_DIAG_MESH owner={} component={} mesh={} world_loc={} world_rot={} world_scale={} relative_loc={} relative_rot={} relative_scale={}".format(
                label, component.get_name(), mesh.get_path_name(),
                vec(transform.translation), rot(transform.rotation.rotator()), vec(transform.scale3d),
                vec(component.get_editor_property("relative_location")),
                rot(component.get_editor_property("relative_rotation")),
                vec(component.get_editor_property("relative_scale3d"))))
    unreal.log("SW_CULL_DIAG_TARGETS={}".format(targets))


main()
