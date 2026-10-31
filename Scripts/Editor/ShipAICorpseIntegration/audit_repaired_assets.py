import json
from pathlib import Path
import unreal
ROOT=Path(unreal.Paths.project_dir()).resolve();OUT=ROOT/'Saved/AssetRepair'
ASSET_AUDIT_OUTPUT_DIR=OUT;ASSET_AUDIT_BASE_REV='d64d6ace'
source=Path(__file__).with_name('audit_assets.py')
exec(compile(source.read_text(encoding='utf8'),str(source),'exec'))
samples=['/Game/Characters/UEFN_Mannequin/Animations/Jump/M_Relaxed_Jump_B_Start_Lfoot','/Game/Characters/UEFN_Mannequin/Animations/Run/M_Neutral_Run_Box_LL_F_Lfoot']
rows=[]
for path in samples:
    asset=unreal.load_asset(path);row={'path':path,'notifies':list(unreal.SWFoleyMigrationLibrary.export_notify_metadata(asset)),'objects':[]}
    for event in unreal.AnimationLibrary.get_animation_notify_events(asset):
        for obj in [event.notify,event.notify_state_class]:
            if obj:
                data={'class':obj.get_class().get_name()}
                for prop in ['database','databases','schema','pose_search_database']:
                    try:data[prop]=str(obj.get_editor_property(prop))
                    except Exception:pass
                row['objects'].append(data)
    rows.append(row)
(OUT/'animation_reference_diagnosis.json').write_text(json.dumps(rows,ensure_ascii=False,indent=2),encoding='utf8')
