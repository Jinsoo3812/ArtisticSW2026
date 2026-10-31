"""Original socket-growth trigger: retained sockets + repeated real FBX reimport."""
import unreal,json
from pathlib import Path
scripts=Path(__file__).parent;out=Path(unreal.Paths.project_saved_dir())/'ChWomanRigValidation';lib=unreal.SWCharacterRigValidationLibrary
mesh=unreal.load_asset('/Game/jiwon/Characters/ChWomanUE5/SKM_ChWoman_UE5')
def description():return json.loads(lib.describe_mesh(mesh))
def unique_sockets(row):
    result={}
    for socket in row['sockets']:
        key=socket['name']
        if key in result:assert socket==result[key],'Conflicting sockets must not be deleted'
        result[key]=socket
    return result
before=description();expected=unique_sockets(before);checks=[]
for iteration in range(3):
    assert lib.ensure_rig_physics(mesh);row=description();assert len(row['sockets'])==len(expected)==13;assert unique_sockets(row)==expected
    checks.append({'operation':'repeat ensure','iteration':iteration,'socket_count':len(row['sockets'])})
assert unreal.EditorAssetLibrary.save_loaded_asset(mesh,False)
for iteration in range(2):
    file=scripts/'import_woman.py';exec(compile(file.read_text(encoding='utf8'),str(file),'exec'));assert report.get('passed'),report
    mesh=unreal.load_asset('/Game/jiwon/Characters/ChWomanUE5/SKM_ChWoman_UE5');row=description()
    assert len(row['sockets'])==13 and unique_sockets(row)==expected
    assert mesh.get_editor_property('post_process_anim_blueprint').get_path_name().endswith('ABP_ChWoman_FinalMarkers_C')
    assert all(b['bone_exists'] and b['shapes']>0 for b in row['bodies'])
    checks.append({'operation':'actual FBX reimport','iteration':iteration,'socket_count':len(row['sockets'])})
male=unreal.load_asset('/Game/jiwon/Characters/ChManUE5/SKM_ChMan_UE5');assert lib.ensure_rig_physics(male)
assert len(json.loads(lib.describe_mesh(male))['sockets'])==13
result={'passed':True,'before_count':len(before['sockets']),'after_count':13,'checks':checks,'male_socket_count':13,'scope':'only identical mesh-local duplicates removed; all socket transforms preserved'}
(out/'socket_reimport_audit.json').write_text(json.dumps(result,indent=2),encoding='utf8')
file=scripts/'audit_woman_final.py';exec(compile(file.read_text(encoding='utf8'),str(file),'exec'))
