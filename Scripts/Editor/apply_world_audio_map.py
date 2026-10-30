"""Author ocean director and collision-only surface materials in Lvl_CY.

Requires rendering: -AllowCommandletRendering -AllowCommandletAudio (no -nullrhi).
Saves only the new audio materials and the requested map; keeps originals intact.
"""
import json
import shutil
from pathlib import Path
import unreal

ROOT = "/Game/Audio/World"
OUT = Path(unreal.Paths.project_saved_dir()).resolve() / "AudioIntegration"
ASSETS = unreal.EditorAssetLibrary
HELPER = unreal.SWAudioAuthoringLibrary
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
worlds = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
assert level.load_level("/Game/Level/Lvl_CY")
world = worlds.get_editor_world()
backup = OUT / "Backups/Lvl_CY_before_audio.umap"
backup.parent.mkdir(parents=True, exist_ok=True)
if not backup.exists():
    shutil.copy2(Path(unreal.Paths.project_content_dir()) / "Level/Lvl_CY.umap", backup)
physical = {name: ASSETS.load_asset(ROOT + "/Surfaces/PM_" + name) for name in ["Sand", "Grass", "Soil", "Wood", "Rock", "Metal"]}
assert all(physical.values())


def duplicate(source, destination):
    if ASSETS.does_asset_exist(destination):
        return ASSETS.load_asset(destination)
    result = ASSETS.duplicate_asset(source, destination)
    assert result, destination
    return result


material = duplicate("/Game/MT/Landscape/M_Landscape", ROOT + "/Surfaces/M_Landscape_Audio")
assert HELPER.configure_landscape_surfaces(material, physical["Sand"], physical["Grass"], physical["Soil"])
assert ASSETS.save_loaded_asset(material, only_if_is_dirty=False)
instance = duplicate("/Game/MT/Landscape/MI_Landscape_02", ROOT + "/Surfaces/MI_Landscape_Audio")
unreal.MaterialEditingLibrary.set_material_instance_parent(instance, material)
assert ASSETS.save_loaded_asset(instance, only_if_is_dirty=False)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
for actor in actors:
    if isinstance(actor, unreal.LandscapeProxy):
        actor.set_editor_property("landscape_material", instance)
        actor.set_editor_property("default_phys_material", physical["Soil"])

# Per-component MI overrides keep third-party source materials and physics tuning intact.
overrides = {}
for actor in actors:
    if isinstance(actor, unreal.LandscapeProxy):
        continue
    for component in actor.get_components_by_class(unreal.StaticMeshComponent):
        for index in range(component.get_num_materials()):
            source = component.get_material(index)
            if not source or not source.get_path_name().startswith("/Game/"):
                continue
            name = source.get_name().lower()
            surface = ("Wood" if any(s in name for s in ["wood", "plank", "hull", "door", "deck"]) else
                       "Rock" if any(s in name for s in ["rock", "stone", "brick", "tile"]) else
                       "Metal" if "metal" in name or "anchor" in name else None)
            if not surface:
                continue
            path = source.get_path_name().split(".")[0]
            if path.startswith(ROOT + "/Surfaces/Instances/"):
                continue
            target = ROOT + "/Surfaces/Instances/" + source.get_name() + "_" + surface
            # Some source packs reuse names; disambiguate by a stable package digest.
            import hashlib
            target += "_" + hashlib.sha1(path.encode()).hexdigest()[:8]
            if target not in overrides:
                if ASSETS.does_asset_exist(target):
                    copy = ASSETS.load_asset(target)
                elif isinstance(source, unreal.MaterialInstanceConstant):
                    copy = duplicate(path, target)
                else:
                    folder, asset_name = target.rsplit("/", 1)
                    copy = unreal.AssetToolsHelpers.get_asset_tools().create_asset(asset_name, folder, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
                    unreal.MaterialEditingLibrary.set_material_instance_parent(copy, source)
                copy.set_editor_property("phys_material", physical[surface])
                assert ASSETS.save_loaded_asset(copy, only_if_is_dirty=False)
                overrides[target] = {"source": path, "surface": surface}
            component.set_material(index, ASSETS.load_asset(target))

directors = [a for a in actors if isinstance(a, unreal.SWOceanAudioDirector)]
assert len(directors) <= 1, "Duplicate ocean audio directors"
if directors:
    director = directors[0]
else:
    gameplay = unreal.get_default_object(unreal.GameplayStatics)
    director = gameplay.call_method("BeginDeferredActorSpawnFromClass", args=(world, unreal.SWOceanAudioDirector.static_class(), unreal.Transform(), unreal.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN, None, unreal.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
    assert director
    gameplay.call_method("FinishSpawningActor", args=(director, unreal.Transform(), unreal.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
director.set_actor_label("OceanAudio_LvlCY")
shore = HELPER.bake_shore_points(world, 2000.0)
assert len(shore) > 20, "Shoreline bake unexpectedly empty"
director.set_editor_property("shore_points", shore)
unreal.log("SW_AUDIO_MAP_BUILD_PHYSICS_BEGIN")
assert HELPER.rebuild_landscape_physics(world), "Landscape physical materials did not finish building"
samples = json.loads(HELPER.sample_terrain_surfaces(world, 4000.0))
(OUT / "surface_samples_latest.json").write_text(json.dumps(samples, indent=2), encoding="utf-8")
assert all(str(i) in samples["counts"] for i in [1, 2, 3]), samples["counts"]
assert unreal.EditorLoadingAndSavingUtils.save_map(world, "/Game/Level/Lvl_CY")
report = {"shore_points": len(shore), "material_overrides": overrides, "terrain_samples": samples,
          "terrain": json.loads(HELPER.inspect_terrain(world))}
(OUT / "map_applied.json").write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
unreal.log("SW_AUDIO_MAP_APPLIED " + json.dumps({"shore_points": len(shore), "surface_counts": samples["counts"], "materials": len(overrides)}))
