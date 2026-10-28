"""Validate the saved map and imported audio without modifying any assets."""
import json
from pathlib import Path
import unreal

OUT = Path(unreal.Paths.project_saved_dir()).resolve() / "AudioIntegration"
assets = unreal.EditorAssetLibrary
profile = assets.load_asset("/Game/Audio/World/DA_WorldAudio")
assert profile and len(profile.get_editor_property("events")) == 15
events = profile.get_editor_property("events")
assert events[unreal.SWWorldSound.SHIP_MOVEMENT].get_editor_property("sound").get_name() == "Cue_Ship_WaveMovement"
assert events[unreal.SWWorldSound.WATER_ENTER].get_editor_property("sound").get_name() == "Cue_CharacterWaterEntry_Heavy"
assert events[unreal.SWWorldSound.CANNON_WATER_IMPACT].get_editor_property("sound").get_name() == "Cue_CannonWaterImpact_Heavy"
manifest = json.loads((OUT / "prepared_manifest.json").read_text(encoding="utf-8"))
for row in manifest:
    relative = Path(row["file"])
    asset = assets.load_asset("/Game/Audio/World/Waves/" + relative.with_suffix("").as_posix())
    assert asset and assets.get_metadata_tag(asset, "SWAudio.SourceSHA256") == row["sha256"], str(relative)
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
current_world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
if not current_world or not current_world.get_path_name().startswith("/Game/Level/Lvl_CY."):
    assert level.load_level("/Game/Level/Lvl_CY")
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
samples = json.loads(unreal.SWAudioAuthoringLibrary.sample_terrain_surfaces(world, 4000.0))
assert all(str(i) in samples["counts"] for i in [1, 2, 3]), samples["counts"]
trace_surfaces = {point.get("trace_surface") for point in samples["points"]}
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
directors = [a for a in actors if isinstance(a, unreal.SWOceanAudioDirector)]
assert len(directors) == 1
shore_count = len(directors[0].get_editor_property("shore_points"))
assert shore_count > 20
ship_manifest = OUT / "ShipWaterPrepared/prepared_manifest.json"
ship_rows = json.loads(ship_manifest.read_text()) if ship_manifest.exists() else []
for row in ship_rows:
    asset = assets.load_asset("/Game/Audio/World/Waves/Ship/" + Path(row["file"]).stem)
    assert asset and assets.get_metadata_tag(asset, "SWAudio.SourceSHA256") == row["sha256"]
report = {"imported_waves": len(manifest) + len(ship_rows), "world_events": 15, "directors": 1, "shore_points": shore_count,
          "terrain_samples": samples, "simple_trace_surfaces": sorted(s for s in trace_surfaces if s is not None)}
(OUT / "saved_map_verified.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
unreal.log("SW_AUDIO_SAVED_MAP_VERIFIED " + json.dumps({"waves": len(manifest), "shore_points": shore_count,
                                                       "surfaces": samples["counts"], "simple_trace_surfaces": report["simple_trace_surfaces"]}))
