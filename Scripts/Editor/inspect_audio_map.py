"""Read-only audio/terrain inventory of Lvl_CY, written to Saved/AudioIntegration."""
import json
from pathlib import Path
import unreal

out = Path(unreal.Paths.project_saved_dir()) / "AudioIntegration"
out.mkdir(parents=True, exist_ok=True)
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert level.load_level("/Game/Level/Lvl_CY")
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
terrain = json.loads(unreal.SWAudioAuthoringLibrary.inspect_terrain(world))
terrain["shore_points"] = len(unreal.SWAudioAuthoringLibrary.bake_shore_points(world, 2000.0))
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
materials = {}
for actor in actors:
    for component in actor.get_components_by_class(unreal.StaticMeshComponent):
        mesh = component.get_editor_property("static_mesh")
        for index in range(component.get_num_materials()):
            material = component.get_material(index)
            if material:
                key = material.get_path_name()
                if key not in materials:
                    materials[key] = {"mesh": mesh.get_path_name() if mesh else None,
                                      "actor": actor.get_actor_label(), "count": 0}
                materials[key]["count"] += 1
terrain["materials"] = materials
registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.search_all_assets(True)
infos = registry.get_assets_by_class(unreal.TopLevelAssetPath("/Script/Landscape", "LandscapeLayerInfoObject"), True)
terrain["layer_info_assets"] = [str(a.package_name) for a in infos]
with (out / "map_before.json").open("w", encoding="utf-8") as f:
    json.dump(terrain, f, indent=2, ensure_ascii=False)
unreal.log("SW_AUDIO_MAP_INSPECTION_COMPLETE " + str(len(materials)))
