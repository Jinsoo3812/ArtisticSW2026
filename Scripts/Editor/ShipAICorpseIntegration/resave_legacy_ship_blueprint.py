"""Drop obsolete serialized native subobject imports through normal BP compilation/save."""
import json,shutil
from pathlib import Path
import unreal
ROOT=Path(unreal.Paths.project_dir()).resolve();OUT=ROOT/'Saved/AssetRepair'
paths=['/Game/jiwon/Test_BP_EnemyShip']+['/Game/GameplayAbilitySystem/Enemy/Balancing/T'+str(t)+'/T'+str(t)+'_BP_EnemyShip' for t in [1,2,3]]
rows=[]
for path in paths:
    file=ROOT/'Content'/(path.removeprefix('/Game/')+'.uasset');backup=OUT/'BeforeShipBlueprints'/file.relative_to(ROOT)
    backup.parent.mkdir(parents=True,exist_ok=True)
    if not backup.exists():shutil.copy2(file,backup)
    bp=unreal.load_asset(path);assert bp and bp.generated_class()
    before=json.loads(unreal.SWAssetRepairLibrary.describe_blueprint_state(bp))
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    after=json.loads(unreal.SWAssetRepairLibrary.describe_blueprint_state(bp))
    assert before==after,'Authored graph/default changed: '+path
    assert unreal.EditorAssetLibrary.save_loaded_asset(bp,False)
    rows.append({'asset':path,'class':bp.generated_class().get_path_name(),'preserved_graph_and_editable_defaults':True})
(OUT/'legacy_ship_blueprint_repair.json').write_text(json.dumps({'passed':True,'assets':rows,'method':'normal compilation/serialization; no graph authoring or component substitutions'},indent=2),encoding='utf8')
