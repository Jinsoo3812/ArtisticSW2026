"""Import/reimport only cannonball-entry audio; preserve character and movement settings."""
import hashlib
import json
from pathlib import Path
import shutil
import unreal

root = Path(unreal.Paths.project_dir()).resolve()
out = root / 'Saved/AudioIntegration/CannonEntryWeight'; out.mkdir(parents=True, exist_ok=True)
prepared = root / 'Saved/AudioIntegration/ShipWaterPrepared'
assets = unreal.EditorAssetLibrary; tools = unreal.AssetToolsHelpers.get_asset_tools()
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
assert not editor or not editor.get_game_world(), 'End PIE before authoring'
folder = '/Game/Audio/World/Waves/Ship'
sound_class = assets.load_asset('/Game/Audio/World/Mix/SC_WorldSFX')
names = ['CannonEntry_PlungeA', 'CannonEntry_PlungeB', 'CannonWaterBody']

def backup(package):
    file = root / 'Content' / (package.removeprefix('/Game/') + '.uasset')
    target = out / 'Backups' / file.relative_to(root / 'Content')
    if file.exists() and not target.exists():
        target.parent.mkdir(parents=True, exist_ok=True); shutil.copy2(file, target)

rows = {Path(row['file']).stem: row for row in json.loads((prepared / 'prepared_manifest.json').read_text())}
waves = []
for name in names:
    package = folder + '/' + name; backup(package)
    wave = assets.load_asset(package)
    expected = rows[name]['sha256']
    if not wave or assets.get_metadata_tag(wave, 'SWAudio.SourceSHA256') != expected:
        task = unreal.AssetImportTask()
        task.filename = str(prepared / (name + '.wav')); task.destination_path = folder; task.destination_name = name
        task.automated = True; task.replace_existing = True; task.replace_existing_settings = False; task.save = False
        tools.import_asset_tasks([task]); wave = assets.load_asset(package)
    assert isinstance(wave, unreal.SoundWave), name
    wave.set_editor_property('looping', False); wave.set_editor_property('sound_class_object', sound_class)
    wave.set_editor_property('priority', 1.)
    assets.set_metadata_tag(wave, 'SWAudio.SourceSHA256', expected)
    assert assets.save_loaded_asset(wave, False)
    waves.append(wave)
cue_path = '/Game/Audio/World/Cues/Cue_CannonWaterImpact_Heavy'; backup(cue_path)
cue = unreal.SWAudioAuthoringLibrary.create_water_impact_cue(cue_path, waves[:2], waves[2], .88)
assert cue
cue.set_editor_property('sound_class_object', sound_class)
assert assets.save_loaded_asset(cue, False)
profile_path = '/Game/Audio/World/DA_WorldAudio'; backup(profile_path)
profile = assets.load_asset(profile_path)
events = dict(profile.get_editor_property('events'))
entry = events[unreal.SWWorldSound.CANNON_WATER_IMPACT]
entry.set_editor_property('sound', cue); entry.set_editor_property('volume', 1.)
events[unreal.SWWorldSound.CANNON_WATER_IMPACT] = entry
profile.set_editor_property('events', events)
assert assets.save_loaded_asset(profile, False)
(out / 'authored.json').write_text(json.dumps({'profile_volume': 1., 'previous_profile_volume': .9,
    'waves': names, 'cue': cue_path, 'source_hashes': {n: rows[n]['sha256'] for n in names}}, indent=2))
unreal.log('SW_CANNON_ENTRY_WEIGHT_AUTHORED')
