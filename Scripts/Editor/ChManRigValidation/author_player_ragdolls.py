"""Author only the two new player PAs from measured source-art descriptors."""
import unreal,json,shutil
from pathlib import Path
root=Path(unreal.Paths.project_dir());out=root/'Saved/PlayerRagdollValidation';out.mkdir(parents=True,exist_ok=True)
lib=unreal.SWCharacterRigValidationLibrary;result={'characters':[]}
for label in ['Man','Woman']:
    descriptor=(root/'SourceArt/Characters/Physics'/('Ch'+label+'_Physics.json')).read_text(encoding='utf8')
    mesh=unreal.load_asset(json.loads(descriptor)['mesh']);assert mesh
    pa=mesh.get_editor_property('physics_asset');assert pa
    for asset in [mesh,pa]:
        relative=asset.get_path_name().split('.')[0].removeprefix('/Game/')+'.uasset';dest=out/'Before'/relative;dest.parent.mkdir(parents=True,exist_ok=True)
        if not dest.exists():shutil.copy2(root/'Content'/relative,dest)
    before=json.loads(lib.describe_ragdoll_physics(mesh));assert lib.configure_ragdoll_physics(mesh,descriptor)
    after=json.loads(lib.describe_ragdoll_physics(mesh));assert len(after['bodies'])==17 and len(after['joints'])==16
    settings=json.loads(descriptor)['world_solver']
    assert all(b['position_iterations']==settings['position_iterations'] and b['velocity_iterations']==settings['velocity_iterations'] and b['shapes']==1 for b in after['bodies'])
    assert all(j['collision_disabled'] for j in after['joints'])
    for asset in [mesh,mesh.get_editor_property('physics_asset')]:assert unreal.EditorAssetLibrary.save_loaded_asset(asset,False)
    result['characters'].append({'name':label,'before':before,'after':after})
assert unreal.EditorAssetLibrary.save_loaded_asset(unreal.load_asset('/Game/jiwon/Characters/Physics/PM_PlayerCorpse'),False)
result['passed']=True;(out/'authored.json').write_text(json.dumps(result,indent=2),encoding='utf8')
