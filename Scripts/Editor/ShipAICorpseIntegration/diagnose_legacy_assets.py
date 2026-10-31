"""Inspect restored originals and NPC animation bindings without asset saves."""
import json,traceback
from pathlib import Path
import unreal
ROOT=Path(unreal.Paths.project_dir()).resolve();OUT=ROOT/'Saved/AssetRepair';OUT.mkdir(parents=True,exist_ok=True)
registry=unreal.AssetRegistryHelpers.get_asset_registry();registry.search_all_assets(True)
report={'restored':[],'npc':{},'errors':[]}
history=json.loads((OUT/'historical_restoration.json').read_text(encoding='utf8'))
for row in history['restored']:
    path=row['package'];asset=unreal.load_asset(path)
    data={'package':path,'loaded':bool(asset),'resolved':asset.get_path_name() if asset else None,'class':asset.get_class().get_name() if asset else None}
    report['restored'].append(data)
for path in ['/Game/Chae_Asset/YiSunSinContent/Animations/ABP_YiSunSin','/Game/Chae_Asset/YiSunSinContent/Animations/BS_YiSunSin_Locomotion','/Game/Chae_Asset/YiSunSinContent/Characters/SK_YiSunSin_Latest','/Game/Chae_Asset/YiSunSinContent/Animations/Latest/AS_YiSunSin_Walk','/Game/Chae_Asset/YiSunSinContent/Animations/Latest/AS_YiSunSin_Run']:
    asset=unreal.load_asset(path);row={'loaded':bool(asset)}
    if asset:
        for prop in ['skeleton','target_skeleton','blend_parameters','sample_data','parent_class']:
            try:
                value=asset.get_editor_property(prop)
                row[prop]=value.export_text() if hasattr(value,'export_text') else str(value)
            except Exception as error:row[prop+'_access_error']=str(error)
        if isinstance(asset,unreal.AnimBlueprint):row['graph']=json.loads(unreal.SWCharacterRigValidationLibrary.describe_graph(asset))
    report['npc'][path]=row
for path in ['/Game/LevelPrototyping/Interactable/Door/Meshes/SM_DoorFrame_Edge','/Engine/EngineSky/BP_Sky_Sphere','/Engine/EngineSky/M_Sky_Panning_Clouds2']:
    asset=unreal.load_asset(path);report.setdefault('canonical_candidates',[]).append({'path':path,'loaded':bool(asset)})
(OUT/'legacy_asset_diagnosis.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
