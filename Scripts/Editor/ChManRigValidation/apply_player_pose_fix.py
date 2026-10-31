"""Update the real player PAs and persist the missing female material slots."""
import unreal,json,shutil
from pathlib import Path
ROOT=Path(unreal.Paths.project_dir()).resolve();OUT=ROOT/'Saved/PlayerRagdollPoseFix';OUT.mkdir(parents=True,exist_ok=True)
LIB=unreal.SWCharacterRigValidationLibrary
report={'project':str(ROOT),'characters':[]}
for label in ['Man','Woman']:
    mesh=unreal.load_asset('/Game/jiwon/Characters/Ch'+label+'UE5/SKM_Ch'+label+'_UE5');assert mesh
    pa=mesh.get_editor_property('physics_asset');assert pa.get_path_name().startswith('/Game/jiwon/Characters/Ch'+label+'UE5/PA_Ch'+label+'_UE5.')
    for asset in [mesh,pa]:
        relative=asset.get_path_name().split('.')[0].removeprefix('/Game/')+'.uasset';dest=OUT/'Before'/relative
        if not dest.exists():dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(ROOT/'Content'/relative,dest)
    before=json.loads(LIB.describe_ragdoll_physics(mesh))
    if label=='Woman':
        slots=mesh.get_editor_property('materials')
        for index,slot in enumerate(slots):
            name=str(slot.get_editor_property('material_slot_name'))
            roles=[r for r in ['Clothes','Hair','Accessory','Bag','Body','Eyes','Shoes'] if r.lower() in name.lower()];assert len(roles)==1,name
            material=unreal.load_asset('/Game/jiwon/Characters/ChGirlMtl/M_ChGirl'+roles[0]);assert material
            slot.set_editor_property('material_interface',material);slots[index]=slot
        mesh.set_editor_property('materials',slots)
    descriptor=(ROOT/'SourceArt/Characters/Physics'/('Ch'+label+'_Physics.json')).read_text(encoding='utf8')
    assert LIB.configure_ragdoll_physics(mesh,descriptor)
    after=json.loads(LIB.describe_ragdoll_physics(mesh));assert before['bodies']==after['bodies'] and before['disabled_pairs']==after['disabled_pairs']
    desired={j['child']:j['swing1_swing2_twist_deg'] for j in json.loads(descriptor)['joints']}
    for joint in after['joints']:assert all(abs(joint[k]-v)<.001 for k,v in zip(['swing1','swing2','twist'],desired[joint['child']]))
    assert all(s.get_editor_property('material_interface') for s in mesh.get_editor_property('materials'))
    for asset in [pa,mesh]:assert unreal.EditorAssetLibrary.save_loaded_asset(asset,False)
    report['characters'].append({'character':label,'before':before,'after':after,'materials':[s.material_interface.get_path_name() for s in mesh.get_editor_property('materials')]})
report['passed']=True;(OUT/'applied.json').write_text(json.dumps(report,indent=2),encoding='utf8')
