"""Read registry metadata and selected feedback assignments; never save assets."""
import json
from pathlib import Path
import unreal

out=Path(unreal.Paths.project_saved_dir()).resolve()/'AudioIntegration/CoverageAudit'
out.mkdir(parents=True,exist_ok=True)
registry=unreal.AssetRegistryHelpers.get_asset_registry()
rows=[]
for name in ['SoundWave','SoundCue','MetaSoundSource','WeaponFeedbackDataAsset']:
    module='ArtisticSWCore' if name=='WeaponFeedbackDataAsset' else ('MetasoundEngine' if name=='MetaSoundSource' else 'Engine')
    for data in registry.get_assets_by_class(unreal.TopLevelAssetPath('/Script/'+module,name),True):
        if str(data.package_name).startswith('/Game/'):
            rows.append({'class':name,'path':str(data.package_name),'name':str(data.asset_name)})
(out/'sound_inventory.json').write_text(json.dumps(rows,indent=2),encoding='utf-8')
unreal.log('SW_AUDIO_COVERAGE_INVENTORY '+str(len(rows)))
