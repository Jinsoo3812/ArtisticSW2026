"""Verify saved character plunge references and exported PCM, without asset writes."""
import hashlib
import json
from pathlib import Path
import unreal

root = Path(unreal.Paths.project_dir()).resolve()
out = root / 'Saved/AudioIntegration/CannonEntryWeight'
authored = json.loads((out / 'authored.json').read_text())
for package, expected in json.loads((out / 'preserved_before.json').read_text()).items():
    assert hashlib.sha256((root / package).read_bytes()).hexdigest() == expected, package
assets = unreal.EditorAssetLibrary
profile = assets.load_asset('/Game/Audio/World/DA_WorldAudio')
entry = dict(profile.get_editor_property('events'))[unreal.SWWorldSound.CANNON_WATER_IMPACT]
assert abs(entry.get_editor_property('volume') - 1.) < .0001
assert entry.get_editor_property('sound').get_path_name().endswith('Cue_CannonWaterImpact_Heavy.Cue_CannonWaterImpact_Heavy')
exports = []
for name, expected in authored['source_hashes'].items():
    wave = assets.load_asset('/Game/Audio/World/Waves/Ship/' + name)
    assert assets.get_metadata_tag(wave, 'SWAudio.SourceSHA256') == expected
    task = unreal.AssetExportTask()
    task.object = wave; task.filename = str(out / (name + '_exported.wav'))
    task.automated = True; task.prompt = False; task.replace_identical = True
    task.exporter = unreal.SoundExporterWAV()
    assert unreal.Exporter.run_asset_export_task(task), name
    exports.append(task.filename)
(out / 'verified.json').write_text(json.dumps({'profile_volume': 1., 'preserved_character_fire_foley_swim': True, 'exports': exports}, indent=2))
unreal.log('SW_CANNON_ENTRY_WEIGHT_VERIFIED')
