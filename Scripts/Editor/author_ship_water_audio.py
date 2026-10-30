"""UE 5.7: import prepared water layers and connect ship/entry sound data."""
import hashlib
import json
from pathlib import Path
import shutil
import unreal

root = Path(unreal.Paths.project_dir()).resolve()
staging = root / "Saved/AudioIntegration"
source = staging / "ShipWaterPrepared"
assets = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
folder = "/Game/Audio/World/Waves/Ship"
sound_class = assets.load_asset("/Game/Audio/World/Mix/SC_WorldSFX")
waves = {}
entries = json.loads((source / "prepared_manifest.json").read_text()) if (source / "prepared_manifest.json").exists() else [
    {"file": n + ".wav", "loop": n.startswith("Loop_")} for n in ["Loop_River_Ambience__Boat1", "Loop_Running_Water2", "Oneshot_Splash3", "Oneshot_Splash5", "WaterImpact_Body", "Oneshot_Swimming_Heavy1", "Oneshot_Wave_Crash4", "CharacterWaterBody", "CannonWaterBody", "CharacterEntry_PlungeA", "CharacterEntry_PlungeB", "CannonEntry_PlungeA", "CannonEntry_PlungeB"]]
for entry in entries:
    path = source / entry["file"]
    name = path.stem
    task = unreal.AssetImportTask()
    task.filename = str(path); task.destination_path = folder; task.destination_name = name
    task.automated = True; task.replace_existing = False; task.save = False
    wave = assets.load_asset(folder + "/" + name)
    if not wave or ("sha256" in entry and assets.get_metadata_tag(wave, "SWAudio.SourceSHA256") != entry["sha256"]):
        assert path.exists(), "Export and prepare sources before first import: " + name
        task.replace_existing = True
        task.replace_existing_settings = False
        tools.import_asset_tasks([task]); wave = assets.load_asset(folder + "/" + name)
    assert isinstance(wave, unreal.SoundWave), name
    wave.set_editor_property("looping", entry["loop"])
    wave.set_editor_property("sound_class_object", sound_class)
    wave.set_editor_property("priority", 0.45 if entry["loop"] else 1.)
    if "sha256" in entry: assets.set_metadata_tag(wave, "SWAudio.SourceSHA256", entry["sha256"])
    assert assets.save_loaded_asset(wave, False)
    waves[name] = wave

profile_path = "/Game/Audio/World/DA_WorldAudio"
original = root / "Content/Audio/World/DA_WorldAudio.uasset"
backup = staging / "ShipWater/Backups/DA_WorldAudio.uasset"
backup.parent.mkdir(parents=True, exist_ok=True)
if not backup.exists(): shutil.copy2(original, backup)
profile = assets.load_asset(profile_path)
events = dict(profile.get_editor_property("events"))
for event, name, attacks, body, pitch in [
    (unreal.SWWorldSound.WATER_ENTER, "Cue_CharacterWaterEntry_Heavy", [waves["CharacterEntry_PlungeA"], waves["CharacterEntry_PlungeB"]], waves['CharacterWaterBody'], .94),
    (unreal.SWWorldSound.CANNON_WATER_IMPACT, "Cue_CannonWaterImpact_Heavy", [waves['CannonEntry_PlungeA'], waves['CannonEntry_PlungeB']], waves['CannonWaterBody'], .88),
]:
    cue = unreal.SWAudioAuthoringLibrary.create_water_impact_cue("/Game/Audio/World/Cues/" + name, attacks, body, pitch)
    assert cue
    cue.set_editor_property("sound_class_object", sound_class)
    assert assets.save_loaded_asset(cue, False)
    value = events[event]; value.set_editor_property("sound", cue)
    if event == unreal.SWWorldSound.WATER_ENTER: value.set_editor_property("volume", .72)
    if event == unreal.SWWorldSound.CANNON_WATER_IMPACT: value.set_editor_property("volume", 1.)
    events[event] = value

mix_folder = "/Game/Audio/World/Mix"
attenuation = assets.load_asset(mix_folder + "/ATT_ShipMovement") or tools.create_asset("ATT_ShipMovement", mix_folder, unreal.SoundAttenuation, unreal.SoundAttenuationFactory())
settings = attenuation.get_editor_property("attenuation")
for key, value in {"attenuate": True, "spatialize": True, "attenuation_shape_extents": unreal.Vector(1200, 0, 0), "falloff_distance": 6800., "attenuate_with_lpf": True, "lpf_radius_min": 2500., "lpf_radius_max": 8000., "lpf_frequency_at_max": 1800.}.items():
    settings.set_editor_property(key, value)
attenuation.set_editor_property("attenuation", settings); assert assets.save_loaded_asset(attenuation, False)
concurrency = assets.load_asset(mix_folder + "/CON_ShipMovement") or tools.create_asset("CON_ShipMovement", mix_folder, unreal.SoundConcurrency, unreal.SoundConcurrencyFactory())
settings = concurrency.get_editor_property("concurrency")
settings.set_editor_property("max_count", 4)
settings.set_editor_property("resolution_rule", unreal.MaxConcurrentResolutionRule.STOP_QUIETEST)
settings.set_editor_property("voice_steal_release_time", .2)
concurrency.set_editor_property("concurrency", settings); assert assets.save_loaded_asset(concurrency, False)
entry = unreal.SWWorldSoundSet()
ship_loop = unreal.SWAudioAuthoringLibrary.create_ship_loop_cue('/Game/Audio/World/Cues/Cue_Ship_WaveMovement',waves['Loop_River_Ambience__Boat1'],assets.load_asset('/Game/Audio/World/Waves/Water/Loop_Ocean_Wave7'))
assert ship_loop
ship_loop.set_editor_property('sound_class_object',sound_class)
ship_loop.set_editor_property('priority',.45)
assert assets.save_loaded_asset(ship_loop,False)
entry.set_editor_property("sound", ship_loop)
entry.set_editor_property("attenuation", attenuation); entry.set_editor_property("concurrency", concurrency)
entry.set_editor_property("volume", .32)
events[unreal.SWWorldSound.SHIP_MOVEMENT] = entry
wade = unreal.SWWorldSoundSet()
wade.set_editor_property("sound", assets.load_asset("/Game/Audio/World/Cues/Footsteps/Cue_Water_Walk"))
wade.set_editor_property("attenuation", assets.load_asset(mix_folder + "/ATT_WaterMovement"))
wade.set_editor_property("concurrency", assets.load_asset(mix_folder + "/CON_WaterMovement"))
wade.set_editor_property("volume", .6)
events[unreal.SWWorldSound.WATER_WADE_ENTER] = wade
wave_cue = unreal.SWAudioAuthoringLibrary.create_random_cue('/Game/Audio/World/Cues/Cue_Ship_WaveImpact',[assets.load_asset('/Game/Audio/World/Waves/Water/Oneshot_Wave_Crash1'),assets.load_asset('/Game/Audio/World/Waves/Water/Oneshot_Wave_Crash3'),waves['Oneshot_Wave_Crash4']])
wave_cue.set_editor_property('sound_class_object',sound_class)
wave_cue.set_editor_property('priority',.45)
assert assets.save_loaded_asset(wave_cue,False)
wave_hit = unreal.SWWorldSoundSet()
wave_concurrency = assets.load_asset(mix_folder+'/CON_ShipWaveImpact') or tools.create_asset('CON_ShipWaveImpact',mix_folder,unreal.SoundConcurrency,unreal.SoundConcurrencyFactory())
wave_settings=wave_concurrency.get_editor_property('concurrency')
wave_settings.set_editor_property('max_count',4)
wave_settings.set_editor_property('resolution_rule',unreal.MaxConcurrentResolutionRule.STOP_QUIETEST)
wave_settings.set_editor_property('voice_steal_release_time',.2)
wave_concurrency.set_editor_property('concurrency',wave_settings)
assert assets.save_loaded_asset(wave_concurrency,False)
wave_hit.set_editor_property('sound',wave_cue)
wave_hit.set_editor_property('attenuation',attenuation)
wave_hit.set_editor_property('concurrency',wave_concurrency)
wave_hit.set_editor_property('volume',.45)
events[unreal.SWWorldSound.SHIP_WAVE_IMPACT] = wave_hit

def bounded_entry(name, sound, inner, distance, voices, volume):
    att=assets.load_asset(mix_folder+'/ATT_'+name) or tools.create_asset('ATT_'+name,mix_folder,unreal.SoundAttenuation,unreal.SoundAttenuationFactory())
    settings=att.get_editor_property('attenuation')
    for key,value in {'attenuate':True,'spatialize':True,'attenuation_shape_extents':unreal.Vector(inner,0,0),'falloff_distance':distance,'attenuate_with_lpf':True,'lpf_radius_min':inner+distance*.25,'lpf_radius_max':inner+distance,'lpf_frequency_at_max':1800.}.items():settings.set_editor_property(key,value)
    att.set_editor_property('attenuation',settings);assert assets.save_loaded_asset(att,False)
    con=assets.load_asset(mix_folder+'/CON_'+name) or tools.create_asset('CON_'+name,mix_folder,unreal.SoundConcurrency,unreal.SoundConcurrencyFactory())
    settings=con.get_editor_property('concurrency')
    settings.set_editor_property('max_count',voices)
    settings.set_editor_property('resolution_rule',unreal.MaxConcurrentResolutionRule.STOP_QUIETEST)
    settings.set_editor_property('voice_steal_release_time',.2)
    con.set_editor_property('concurrency',settings);assert assets.save_loaded_asset(con,False)
    value=unreal.SWWorldSoundSet()
    for key,obj in {'sound':sound,'attenuation':att,'concurrency':con,'volume':volume}.items():value.set_editor_property(key,obj)
    return value

events[unreal.SWWorldSound.SHIP_LEAK]=bounded_entry('ShipLeak',waves['Loop_Running_Water2'],150.,2350.,3,.22)
gulls=unreal.SWAudioAuthoringLibrary.create_random_cue('/Game/Audio/World/Cues/Cue_Seagull_Coast',[assets.load_asset('/Game/Audio/World/Waves/Water/Oneshot_Seagull1'),assets.load_asset('/Game/Audio/World/Waves/Water/Oneshot_Seagull2')])
gulls.set_editor_property('sound_class_object',sound_class)
gulls.set_editor_property('priority',.3)
assert assets.save_loaded_asset(gulls,False)
events[unreal.SWWorldSound.SEAGULL]=bounded_entry('CoastalWildlife',gulls,200.,5800.,1,.32)
mix=assets.load_asset(mix_folder+'/Mix_Cabin') or tools.create_asset('Mix_Cabin',mix_folder,unreal.SoundMix,unreal.SoundMixFactory())
mix.set_editor_property('sound_class_effects',[unreal.SoundClassAdjuster(sound_class_object=assets.load_asset(mix_folder+'/SC_Weather'),volume_adjuster=.2,low_pass_filter_frequency=1800.)])
mix.set_editor_property('fade_in_time',.6);mix.set_editor_property('fade_out_time',.6)
assert assets.save_loaded_asset(mix,False)
profile.set_editor_property('cabin_mix',mix)
profile.set_editor_property("events", events); assert assets.save_loaded_asset(profile, False)
(staging / "ShipWater/authored.json").write_text(json.dumps({"profile": profile.get_path_name(), "new_waves": list(waves), "original_profile_sha256": hashlib.sha256(backup.read_bytes()).hexdigest(), "ship_voice_limit": 4, "range_cm": 8000}, indent=2), encoding="utf-8")
unreal.log("SW_SHIP_WATER_AUTHORED")
