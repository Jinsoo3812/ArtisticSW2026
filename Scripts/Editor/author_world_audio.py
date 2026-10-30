"""Create UE 5.7 project audio from prepared WAVs. Run after export_audio_sources and prepare_audio_wavs.

Default stages audio assets only. -AudioApplyMap also authors Lvl_CY.
Source packs are never mounted or saved in this process.
"""
import json
from pathlib import Path
import unreal

ROOT = "/Game/Audio/World"
STAGING = Path(unreal.Paths.project_saved_dir()).resolve() / "AudioIntegration"
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
ASSETS = unreal.EditorAssetLibrary
HELPER = unreal.SWAudioAuthoringLibrary
CHANGED = []


def save(asset):
    assert ASSETS.save_loaded_asset(asset, only_if_is_dirty=False), asset.get_path_name()
    CHANGED.append(asset.get_path_name())
    return asset


def create(path, cls, factory=None):
    existing = ASSETS.load_asset(path) if ASSETS.does_asset_exist(path) else None
    if existing:
        assert isinstance(existing, cls), path
        return existing
    folder, name = path.rsplit("/", 1)
    if factory is None:
        factory = unreal.DataAssetFactory()
        factory.set_editor_property("data_asset_class", cls)
    asset = TOOLS.create_asset(name, folder, cls, factory)
    assert asset, path
    return asset


def sound_class(name, submix):
    asset = create(ROOT + "/Mix/" + name, unreal.SoundClass, unreal.SoundClassFactory())
    props = asset.get_editor_property("properties")
    props.set_editor_property("default_submix", submix)
    asset.set_editor_property("properties", props)
    return save(asset)


def attenuation(name, inner, falloff):
    asset = create(ROOT + "/Mix/" + name, unreal.SoundAttenuation, unreal.SoundAttenuationFactory())
    settings = asset.get_editor_property("attenuation")
    settings.set_editor_property("attenuate", True)
    settings.set_editor_property("spatialize", True)
    settings.set_editor_property("attenuation_shape_extents", unreal.Vector(inner, 0, 0))
    settings.set_editor_property("falloff_distance", falloff)
    settings.set_editor_property("attenuate_with_lpf", True)
    settings.set_editor_property("lpf_radius_min", inner + falloff * 0.25)
    settings.set_editor_property("lpf_radius_max", inner + falloff)
    asset.set_editor_property("attenuation", settings)
    return save(asset)


def concurrency(name, count):
    asset = create(ROOT + "/Mix/" + name, unreal.SoundConcurrency, unreal.SoundConcurrencyFactory())
    settings = asset.get_editor_property("concurrency")
    settings.set_editor_property("max_count", count)
    settings.set_editor_property("resolution_rule", unreal.MaxConcurrentResolutionRule.STOP_QUIETEST)
    settings.set_editor_property("voice_steal_release_time", 0.06)
    asset.set_editor_property("concurrency", settings)
    return save(asset)


def import_waves():
    manifest = json.loads((STAGING / "prepared_manifest.json").read_text(encoding="utf-8"))
    waves = {}
    for record in manifest:
        relative = Path(record["file"])
        folder = ROOT + "/Waves/" + relative.parent.as_posix()
        path = folder + "/" + relative.stem
        wave = ASSETS.load_asset(path) if ASSETS.does_asset_exist(path) else None
        if not wave or ASSETS.get_metadata_tag(wave, "SWAudio.SourceSHA256") != record["sha256"]:
            task = unreal.AssetImportTask()
            task.filename = str(STAGING / "Prepared" / relative)
            task.destination_path = folder
            task.destination_name = relative.stem
            task.automated = True
            task.replace_existing = True
            task.save = False
            task.factory = unreal.SoundFactory()
            TOOLS.import_asset_tasks([task])
            assert task.imported_object_paths, str(relative)
            wave = ASSETS.load_asset(task.imported_object_paths[0])
        assert isinstance(wave, unreal.SoundWave), path
        wave.set_editor_property("looping", record["loop"])
        wave.set_editor_property("virtualization_mode", unreal.VirtualizationMode.PLAY_WHEN_SILENT if record["loop"] else unreal.VirtualizationMode.DISABLED)
        wave.set_editor_property("volume", 1.0)
        wave.set_editor_property("compression_quality", 80 if record["loop"] else 90)
        ASSETS.set_metadata_tag(wave, "SWAudio.SourceSHA256", record["sha256"])
        save(wave)
        waves[relative.as_posix()] = wave
    return waves


def cue(name, waves, cls):
    asset = HELPER.create_random_cue(ROOT + "/Cues/" + name, waves)
    assert asset, name
    asset.set_editor_property("sound_class_object", cls)
    asset.set_editor_property("volume_multiplier", 0.35 if name.startswith("Footsteps/") else 1.0)
    save(asset)
    return asset


def author_assets():
    waves = import_waves()
    submix = ASSETS.load_asset("/Game/Audio/Mix/SM_Final")
    classes = {name: sound_class("SC_" + name, submix) for name in ["WorldSFX", "Foley", "Environment"]}
    far = attenuation("ATT_Cannon", 350.0, 45000.0)
    splash = attenuation("ATT_WaterImpact", 150.0, 14000.0)
    near = attenuation("ATT_WaterMovement", 80.0, 2200.0)
    gun_limit = concurrency("CON_Cannon", 8)
    splash_limit = concurrency("CON_WaterImpact", 12)
    movement_limit = concurrency("CON_WaterMovement", 12)
    sets = {}
    definitions = {
        "CANNON_FIRE": ([k for k in waves if k.startswith("Cannon/")], far, gun_limit, 0.65),
        "CANNON_WATER_IMPACT": (["Water/Oneshot_Splash2.wav", "Water/Oneshot_Splash4.wav", "Water/Oneshot_Splash6.wav"], splash, splash_limit, 0.9),
        "WATER_BOMB_BURST": (["Water/Oneshot_Splash1.wav", "Water/Oneshot_Splash4.wav"], splash, splash_limit, 0.8),
        "WATER_ENTER": (["Water/Oneshot_Diving1.wav", "Water/Oneshot_Diving2.wav"], near, movement_limit, 0.55),
        "WATER_EXIT": (["Water/Oneshot_Waterdrop1.wav", "Water/Oneshot_Waterdrop2.wav"], near, movement_limit, 0.4),
        "SWIM_STROKE": (["Water/Oneshot_Swimming_Stroke1.wav", "Water/Oneshot_Swimming_Stroke2.wav"], near, movement_limit, 0.45),
        "UNDERWATER_STROKE": (["Water/Oneshot_Underwater_swimming1.wav", "Water/Oneshot_Underwater_swimming2.wav"], near, movement_limit, 0.4),
    }
    for event, (keys, att, limit, volume) in definitions.items():
        sound = cue("Cue_" + event.title().replace("_", ""), [waves[k] for k in keys], classes["WorldSFX"])
        item = unreal.SWWorldSoundSet()
        item.set_editor_property("sound", sound)
        item.set_editor_property("attenuation", att)
        item.set_editor_property("concurrency", limit)
        item.set_editor_property("volume", volume)
        sets[getattr(unreal.SWWorldSound, event)] = item
    for event, key, volume in [("OCEAN", "Water/Loop_Ocean_Wave5.wav", 0.24),
                               ("SHORE", "Water/Loop_Sea_Shore_Ambience1.wav", 0.22),
                               ("UNDERWATER", "Water/Loop_Underwater1.wav", 0.35)]:
        sound = waves[key]
        sound.set_editor_property("sound_class_object", classes["Environment"])
        save(sound)
        item = unreal.SWWorldSoundSet()
        item.set_editor_property("sound", sound)
        item.set_editor_property("volume", volume)
        sets[getattr(unreal.SWWorldSound, event)] = item
    mix = create(ROOT + "/Mix/Mix_Underwater", unreal.SoundMix, unreal.SoundMixFactory())
    adjusters = []
    for cls in [classes["WorldSFX"], classes["Foley"]]:
        adjust = unreal.SoundClassAdjuster()
        adjust.set_editor_property("sound_class_object", cls)
        adjust.set_editor_property("volume_adjuster", 0.6)
        adjust.set_editor_property("low_pass_filter_frequency", 1400.0)
        adjusters.append(adjust)
    mix.set_editor_property("sound_class_effects", adjusters)
    mix.set_editor_property("fade_in_time", 0.5)
    mix.set_editor_property("fade_out_time", 0.6)
    mix.set_editor_property("duration", -1.0)
    save(mix)
    profile = create(ROOT + "/DA_WorldAudio", unreal.SWWorldAudioProfile)
    profile.set_editor_property("events", sets)
    profile.set_editor_property("underwater_mix", mix)
    save(profile)
    physical = {}
    surface_cues = {}
    for index, (surface, folder) in enumerate([("Sand", "SAND"), ("Soil", "SOIL"), ("Grass", "GRASS"), ("Water", "WATER"),
                                               ("Wood", "WOOD"), ("Rock", "ROCKS"), ("Metal", "METAL_1")], 1):
        pm = create(ROOT + "/Surfaces/PM_" + surface, unreal.PhysicalMaterial, unreal.PhysicalMaterialFactoryNew())
        pm.set_editor_property("surface_type", getattr(unreal.PhysicalSurface, "SURFACE_TYPE" + str(index)))
        save(pm)
        physical[surface] = pm
        for action in ["Walk", "Run", "Jump", "Land"]:
            prefix = "Footsteps/" + folder + "/" + ("JUMP_LAND" if action in ("Jump", "Land") else action.upper()) + "/"
            keys = [k for k in waves if k.startswith(prefix) and (action not in ("Jump", "Land") or action.upper() in k)]
            surface_cues[(surface, action)] = cue("Footsteps/Cue_" + surface + "_" + action, [waves[k] for k in keys], classes["Foley"])
    foley = ASSETS.load_asset("/Game/Audio/Foley/DA_Mannequin_Foley")
    assert foley
    events = foley.get_editor_property("events")
    for tag, definition in list(events.items()):
        tag_name = str(tag.get_editor_property("tag_name")).split(".")[-1]
        action = {"Walk": "Walk", "WalkBackwds": "Walk", "Run": "Run", "RunBackwds": "Run", "RunStrafe": "Run",
                  "Jump": "Jump", "Land": "Land", "Scuff": "Walk", "ScuffPivot": "Walk"}.get(tag_name)
        if not action:
            continue
        definition.set_editor_property("surface_sounds", {pm.get_editor_property("surface_type"): surface_cues[(surface, action)]
                                                          for surface, pm in physical.items()})
        definition.set_editor_property("default_sound", surface_cues[("Soil", action)])
        definition.set_editor_property("trace_surface", True)
        events[tag] = definition
    foley.set_editor_property("events", events)
    save(foley)
    return physical


physical = author_assets()
weather_script = Path(__file__).with_name("configure_weather_audio.py")
exec(compile(weather_script.read_text(encoding="utf-8"), str(weather_script), "exec"), {"__name__": "__main__"})
CHANGED.extend(["/Game/Audio/World/Mix/SC_Weather", "/Game/StylizedWeather/EpicContents/Rain_Heavy_Ext01",
                "/Game/StylizedWeather/EpicContents/Rain_Heavy_Ext01_Cue"])
ship_script = Path(__file__).with_name("author_ship_water_audio.py")
exec(compile(ship_script.read_text(encoding="utf-8"), str(ship_script), "exec"), {"__name__": "__main__"})
material_helper = unreal.RealisticWaterMaterialPipelineLibrary
graph = {}
for path in ["/Game/MT/Landscape/M_Landscape", "/Game/MT/Landscape/MF_LandscapeLayerA", "/Game/MT/Landscape/MF_LandscapeLayerC",
             "/Game/MT/Landscape/MF_LandscapeLayerD", "/Game/MT/Landscape/MF_LandscapeLayerE", "/Game/MT/Landscape/MF_SlopeMask"]:
    asset = ASSETS.load_asset(path)
    expressions = material_helper.get_material_expressions(asset) if isinstance(asset, unreal.Material) else material_helper.get_material_function_expressions(asset)
    nodes = []
    for node in expressions:
        item = {"name": node.get_name(), "class": node.get_class().get_name(), "inputs": {}}
        for prop in ["parameter_name", "default_value", "layer_name", "material_function", "desc", "const_a", "const_b"]:
            try:
                item[prop] = str(node.get_editor_property(prop))
            except Exception:
                pass
        for index in range(12):
            source = material_helper.get_connected_input_expression(node, index)
            if source:
                item["inputs"][index] = source.get_name()
        nodes.append(item)
    graph[path] = nodes
with (STAGING / "landscape_graph.json").open("w", encoding="utf-8") as f:
    json.dump(graph, f, indent=2)
with (STAGING / "authored_assets.json").open("w", encoding="utf-8") as f:
    json.dump(sorted(set(CHANGED)), f, indent=2)
unreal.log("SW_AUDIO_ASSETS_COMPLETE " + str(len(set(CHANGED))))
if "-AudioVerifyMap" in unreal.SystemLibrary.get_command_line():
    script = Path(__file__).with_name("validate_world_audio.py")
    exec(compile(script.read_text(encoding="utf-8"), str(script), "exec"), {})
