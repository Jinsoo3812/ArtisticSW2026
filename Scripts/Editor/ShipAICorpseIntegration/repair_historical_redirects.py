import json
from pathlib import Path
import unreal
ROOT=Path(unreal.Paths.project_dir()).resolve();OUT=ROOT/'Saved/AssetRepair'
plan=json.loads((OUT/'canonical_redirect_plan.json').read_text(encoding='utf8'))
result={'aliases':[],'errors':[]}
for row in plan:
    destination=unreal.load_asset(row['to']);assert destination,row
    assert unreal.SWAssetRepairLibrary.create_historical_redirector(row['from'],destination),row
    source=unreal.load_asset(row['from']);assert source==destination,row
    if isinstance(destination,unreal.Blueprint):
        old_class=row['from']+'.'+row['from'].split('/')[-1]+'_C'
        assert unreal.load_class(None,old_class)==destination.generated_class(),old_class
    result['aliases'].append({**row,'resolved':source.get_path_name()})
result['passed']=True
(OUT/'redirect_repair.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
