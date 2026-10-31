"""Re-save only unresolved imports; retain all loaded notify/animation content."""
import json,shutil
from pathlib import Path
import unreal
ROOT=Path(unreal.Paths.project_dir()).resolve();OUT=ROOT/'Saved/AssetRepair'
old=json.loads((ROOT/'Saved/ShipAICorpseIntegration/engine_asset_audit.json').read_text(encoding='utf8'))['missing_hard_references']
targets={'/Game/Blueprints/AnimNotifies/BP_NotifyState_EarlyTransition',
 '/Game/Characters/UEFN_Mannequin/Animations/ExperimentalStateMachineData/PSD_SM_CMC_Idles',
 '/Game/Characters/UEFN_Mannequin/Animations/ExperimentalStateMachineData/PSD_SM_Mover_TraversalTransitions'}
packages=sorted({r['from'] for r in old if r['to'] in targets})
rows=[]
for package in packages:
    asset=unreal.load_asset(package);assert isinstance(asset,unreal.AnimSequence)
    file=ROOT/'Content'/(package.removeprefix('/Game/')+'.uasset')
    backup=OUT/'BeforeNormalize'/file.relative_to(ROOT);backup.parent.mkdir(parents=True,exist_ok=True)
    if not backup.exists():shutil.copy2(file,backup)
    before=list(unreal.SWFoleyMigrationLibrary.export_notify_metadata(asset))
    assert unreal.EditorAssetLibrary.save_loaded_asset(asset,False)
    after=list(unreal.SWFoleyMigrationLibrary.export_notify_metadata(asset))
    assert before==after,'Notify content changed: '+package
    rows.append({'package':package,'notify_events':len(before),'loaded_notify_metadata_unchanged':True})
(OUT/'reference_normalization.json').write_text(json.dumps({'passed':True,'packages':rows,'method':'canonical serialization of already-null unresolved optional imports; no notify removal, pose/curve edits or graph changes'},indent=2),encoding='utf8')
