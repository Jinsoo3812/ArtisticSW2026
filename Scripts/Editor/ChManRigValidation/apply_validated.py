"""Apply only the exact migration that passed the real PIE comparison."""
import unreal,json,traceback
from pathlib import Path
OUT=Path(unreal.Paths.project_saved_dir())/'ChManRigValidation'
result={}
try:
    evidence=json.loads((OUT/'pie_comparison.json').read_text(encoding='utf8'))
    assert not evidence['errors']
    assert 'NetMode=0' in evidence['world']
    by_name={c['name']:c for c in evidence['cases']}
    for name in ['new_mesh_offsets','new_mesh_clean','new_mesh_clean_30','new_mesh_clean_120']:
        c=by_name[name]
        assert c['frames']>500 and len(c['phase_stats'])==12
        assert c['nonfinite']==0 and c['max_scale_error']<.001 and c['max_radius_cm']<250 and c['max_limb_length_error_cm']<1
        assert not c['outliers']
        assert c['phase_stats']['walk']['max_speed']>100 and c['phase_stats']['run']['max_speed']>300
        assert c['phase_stats']['jump_forward']['air_frames']>5
        assert len(c['animations'])>=4
        assert all(p['invalid_active_mm_frames']==0 for p in c['phase_stats'].values())
    lib=unreal.SWCharacterRigValidationLibrary
    bp=unreal.load_asset('/Game/Blueprints/Player/BP_Player_Man')
    abp=unreal.load_asset('/Game/Anim_Logic/ABP_Player_Man')
    mesh=unreal.load_asset('/Game/jiwon/Characters/ChManUE5/SKM_ChMan_UE5')
    before=json.loads(lib.describe_graph(abp))
    audited=json.loads((OUT/'graph_before.json').read_text(encoding='utf8'))
    expected=['AnimGraphNode_ModifyBone_'+str(i) for i in range(7)]
    fixed=[n['name'] for n in before['nodes'] if n['class']=='AnimGraphNode_ModifyBone']
    if fixed:
        assert set(fixed)==set(expected),'Graph changed since audit; do not alter unrelated controls'
        old={n['name']:n for n in audited['nodes']}
        for node in before['nodes']:
            if node['name'] in fixed:
                assert node['guid']==old[node['name']]['guid'] and node.get('settings')==old[node['name']].get('settings'),'Correction changed since comparison'
        assert lib.bypass_fixed_bone_corrections(abp,expected)
    assert lib.configure_character(bp,mesh,abp)
    assert unreal.EditorAssetLibrary.save_loaded_asset(abp,False)
    assert unreal.EditorAssetLibrary.save_loaded_asset(bp,False)
    after=json.loads(lib.describe_graph(abp));(OUT/'graph_after.json').write_text(json.dumps(after,indent=2),encoding='utf8')
    assert after['preview_mesh']==mesh.get_path_name()
    assert not any(n['class']=='AnimGraphNode_ModifyBone' for n in after['nodes'])
    for name in ['AnimGraphNode_FootPlacement','AnimGraphNode_LegIK','AnimGraphNode_Fabrik','AnimGraphNode_SWCannonOperator','AnimGraphNode_MotionMatching','AnimGraphNode_OrientationWarping']:
        assert sum(n['class']==name for n in before['nodes'])==sum(n['class']==name for n in after['nodes']),name
    cdo=unreal.get_default_object(bp.generated_class());component=cdo.get_editor_property('mesh')
    assert component.get_editor_property('skeletal_mesh_asset')==mesh
    assert component.get_editor_property('anim_class')==abp.generated_class()
    result={'applied':True,'character':bp.get_path_name(),'mesh':mesh.get_path_name(),'animation':abp.get_path_name(),'removed_fixed_nodes':expected,'before_graph_nodes':len(before['nodes']),'after_graph_nodes':len(after['nodes']),'mesh_relative_transform':component.get_relative_transform().export_text()}
except Exception:
    result['error']=traceback.format_exc();unreal.log_error(result['error'])
(OUT/'applied.json').write_text(json.dumps(result,indent=2),encoding='utf8')
