"""Restore only the five tagged barriers to the transforms used by the accepted 10 cm bake."""
import unreal


LEVEL = "/Game/Level/Test_Level"
ACCEPTED_LOCATIONS = {
    "Cube": unreal.Vector(90140.0, 78340.0, 4550.0),
    "Cube2": unreal.Vector(88300.0, 78350.0, 4550.0),
    "Cube3": unreal.Vector(89270.0, 78420.0, 4710.0),
    "Cube5": unreal.Vector(89340.0, 77990.0, 4540.0),
    "Cube6": unreal.Vector(89340.0, 78840.0, 4540.0),
}


def main():
    unreal.EditorLoadingAndSavingUtils.load_map(LEVEL)
    subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    actors = subsystem.get_all_level_actors()
    restored = []
    for actor in actors:
        label = actor.get_actor_label()
        tags = {str(tag) for tag in actor.tags}
        if label in ACCEPTED_LOCATIONS and "SW_CabinBarrier" in tags:
            actor.set_actor_location(ACCEPTED_LOCATIONS[label], False, False)
            restored.append(label)
    if set(restored) != set(ACCEPTED_LOCATIONS):
        raise RuntimeError("Tagged barrier set mismatch: {}".format(restored))
    if not unreal.EditorLoadingAndSavingUtils.save_current_level():
        raise RuntimeError("Could not save restored Test_Level")
    unreal.log("SW_CABIN_BARRIERS_RESTORED={}".format(",".join(sorted(restored))))


main()
