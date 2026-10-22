"""Assign PCGVolumeBounds only when it preserves the audited collision settings.

Run with UnrealEditor-Cmd -run=pythonscript -script=<this file>.
Backups and before/after reports go to Saved/PCG_Profile_Migration.
Does not call PCG Generate/Cleanup or alter generated mesh collision.
"""
import json
import shutil
from pathlib import Path
import unreal

PROFILE = "PCGVolumeBounds"
MAPS = ["/Game/Level/EnemyTest/LV_ET", "/Game/Level/Lvl_CY"]
CHANNELS = [(name, getattr(unreal.CollisionChannel, name))
            for name in dir(unreal.CollisionChannel)
            if name.startswith("ECC_") and name not in ("ECC_MAX", "ECC_OVERLAP_ALL_DEPRECATED")]
OUTPUT = Path(unreal.Paths.project_saved_dir()) / "PCG_Profile_Migration"
OUTPUT.mkdir(parents=True, exist_ok=True)


def collision_settings(component):
    return {
        "enabled": str(component.get_collision_enabled()),
        "object_type": str(component.get_collision_object_type()),
        "overlap_events": component.get_editor_property("generate_overlap_events"),
        "responses": {name: str(component.get_collision_response_to_channel(channel))
                      for name, channel in CHANNELS},
    }


def volume_state(actor):
    pcg = actor.get_editor_property("pcg_component")
    graph_instance = pcg.get_editor_property("graph_instance")
    graph = graph_instance.get_editor_property("graph") if graph_instance else None
    # Relative transforms remain stable across package reloads.
    return {
        "graph": graph.get_path_name() if graph else None,
        "generated": pcg.get_editor_property("generated"),
        "dirty_generated": pcg.get_editor_property("dirty_generated"),
        "primitives": {
            component.get_name(): {
                "collision": collision_settings(component),
                "transform": component_transform(component),
                "instances": component.get_instance_count()
                if isinstance(component, unreal.InstancedStaticMeshComponent) else None,
            }
            for component in actor.get_components_by_class(unreal.PrimitiveComponent)
        },
    }


def component_transform(component):
    location = component.get_editor_property("relative_location")
    rotation = component.get_editor_property("relative_rotation")
    scale = component.get_editor_property("relative_scale3d")
    return [location.x, location.y, location.z, rotation.pitch, rotation.yaw, rotation.roll,
            scale.x, scale.y, scale.z]


report = []
for map_path in MAPS:
    world = unreal.EditorLoadingAndSavingUtils.load_map(map_path)
    if not world:
        raise RuntimeError("Cannot load " + map_path)
    actors = [actor for actor in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
              if isinstance(actor, unreal.PCGVolume)]
    if len(actors) != 1 or actors[0].get_actor_label() != "PCG_Biome":
        raise RuntimeError("Audited PCG_Biome not found uniquely in " + map_path)
    actor = actors[0]
    actor_name = actor.get_name()
    brush = actor.get_editor_property("brush_component")
    before = volume_state(actor)
    profile_before = str(brush.get_collision_profile_name())
    if profile_before not in ("Custom", PROFILE):
        raise RuntimeError("Unexpected existing profile: " + profile_before)
    expected = collision_settings(brush)
    if brush.get_collision_enabled() != unreal.CollisionEnabled.QUERY_ONLY or \
            brush.get_collision_object_type() != unreal.CollisionChannel.ECC_WORLD_STATIC:
        raise RuntimeError("Unexpected PCG collision mode/object type")
    for name, channel in CHANNELS:
        response = unreal.CollisionResponseType.ECR_BLOCK if name == "ECC_ARROW" else unreal.CollisionResponseType.ECR_IGNORE
        if brush.get_collision_response_to_channel(channel) != response:
            raise RuntimeError("Unexpected authored response: " + name)
    source = Path(unreal.Paths.project_content_dir()) / (map_path.removeprefix("/Game/") + ".umap")
    backup = OUTPUT / (source.stem + ".before.umap")
    if not backup.exists():
        shutil.copy2(source, backup)
    try:
        brush.modify()
        brush.set_collision_profile_name(PROFILE)
        if str(brush.get_collision_profile_name()) != PROFILE or collision_settings(brush) != expected:
            raise RuntimeError("Profile does not preserve current collision settings")
        if volume_state(actor) != before:
            raise RuntimeError("PCG graph/generated state/components changed")
        if not unreal.EditorLoadingAndSavingUtils.save_map(world, map_path):
            raise RuntimeError("Cannot save " + map_path)
    except Exception:
        shutil.copy2(backup, source)
        raise
    unreal.EditorLoadingAndSavingUtils.load_map(map_path)
    reloaded = next(a for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
                    if isinstance(a, unreal.PCGVolume) and a.get_name() == actor_name)
    if str(reloaded.get_editor_property("brush_component").get_collision_profile_name()) != PROFILE or \
            volume_state(reloaded) != before:
        shutil.copy2(backup, source)
        raise RuntimeError("Saved profile/state failed reload verification")
    report.append({"map": map_path, "actor": reloaded.get_name(), "old_profile": profile_before,
                   "new_profile": PROFILE, "backup": str(backup), "preserved_state": before})
    (OUTPUT / "report.json").write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
    unreal.log("PCG_PROFILE_APPLIED " + map_path)
