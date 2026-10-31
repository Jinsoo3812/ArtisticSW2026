import unreal,json,traceback,shutil
from pathlib import Path
ROOT=Path(unreal.Paths.project_dir());OUT=ROOT/'Saved/ChManRigValidation';OUT.mkdir(parents=True,exist_ok=True)
report={}
try:
    lib=unreal.SWCharacterRigValidationLibrary
    original=unreal.load_asset('/Game/Anim_Logic/ABP_Player_Man')
    mesh=unreal.load_asset('/Game/jiwon/Characters/ChManUE5/SKM_ChMan_UE5')
    old=unreal.load_asset('/Game/jiwon/Characters/SKM_Player_Man')
    for path in ['Anim_Logic/ABP_Player_Man.uasset','Blueprints/Player/BP_Player_Man.uasset','jiwon/Characters/ChManUE5/SKM_ChMan_UE5.uasset','jiwon/Characters/ChManUE5/SKM_ChMan_UE5_Skeleton.uasset']:
        backup=OUT/'Before'/path;backup.parent.mkdir(parents=True,exist_ok=True)
        if not backup.exists():shutil.copy2(ROOT/'Content'/path,backup)
    graph=json.loads(lib.describe_graph(original))
    (OUT/'graph_before.json').write_text(json.dumps(graph,indent=2),encoding='utf8')
    report['old_mesh']=json.loads(lib.describe_mesh(old))
    report['new_mesh']=json.loads(lib.describe_mesh(mesh))
    report['correction_nodes']=[{'name':n['name'],'settings':n.get('settings'),'pins':n['pins']} for n in graph['nodes'] if n['class']=='AnimGraphNode_ModifyBone']
    dest='/Game/Developers/ChManRigValidation/ABP_Man_NoFixedRigOffsets'
    clean=unreal.load_asset(dest) if unreal.EditorAssetLibrary.does_asset_exist(dest) else unreal.EditorAssetLibrary.duplicate_asset('/Game/Anim_Logic/ABP_Player_Man',dest)
    assert clean
    names=['AnimGraphNode_ModifyBone_'+str(i) for i in range(7)]
    existing=json.loads(lib.describe_graph(clean))
    if any(n['class']=='AnimGraphNode_ModifyBone' for n in existing['nodes']):
        assert lib.bypass_fixed_bone_corrections(clean,names),'Fixed-node bypass preflight/compile failed'
    assert unreal.EditorAssetLibrary.save_loaded_asset(clean,False)
    report['candidate']=clean.get_path_name()
    (OUT/'graph_candidate.json').write_text(lib.describe_graph(clean),encoding='utf8')
    assert lib.ensure_rig_physics(mesh),'Physics setup failed'
    assert unreal.EditorAssetLibrary.save_loaded_asset(mesh.get_editor_property('physics_asset'),False)
    assert unreal.EditorAssetLibrary.save_loaded_asset(mesh,False)
    report['prepared_new_mesh']=json.loads(lib.describe_mesh(mesh))
except Exception:
    report['error']=traceback.format_exc()
(OUT/'candidates.json').write_text(json.dumps(report,indent=2),encoding='utf8')
