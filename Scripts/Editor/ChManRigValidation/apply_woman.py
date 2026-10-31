import unreal,json
from pathlib import Path
from collections import Counter
out=Path(unreal.Paths.project_saved_dir())/'ChWomanRigValidation';lib=unreal.SWCharacterRigValidationLibrary
comparison=json.loads((out/'pie_comparison.json').read_text(encoding='utf8'))
assert not comparison['errors'] and len(comparison['cases'])==5
for case in comparison['cases'][2:]:
    assert case['nonfinite']==0 and not case['outliers'] and case['max_limb_length_error_cm']<1
    assert case['phase_stats']['sprint']['max_speed']>600 and case['phase_stats']['jump_forward']['air_frames']>10
    assert all(p['invalid_active_mm_frames']==0 for name,p in case['phase_stats'].items() if name!='idle')
    assert any('PSD_Sprint_Locomotion' in name for name in case['databases'])
bp=unreal.load_asset('/Game/Blueprints/Player/BP_Player_Woman');abp=unreal.load_asset('/Game/Anim_Logic/ABP_Player_Woman')
mesh=unreal.load_asset('/Game/jiwon/Characters/ChWomanUE5/SKM_ChWoman_UE5');before=json.loads(lib.describe_graph(abp))
original=json.loads((out/'source_asset_audit.json').read_text(encoding='utf8'))['graph']
remaining=[n for n in before['nodes'] if n['class']=='AnimGraphNode_ModifyBone']
if remaining:
    assert {n['name']:n['settings'] for n in remaining}=={n['name']:n['settings'] for n in original['nodes'] if n['class']=='AnimGraphNode_ModifyBone'}
    assert lib.bypass_fixed_bone_corrections(abp,['AnimGraphNode_ModifyBone_0','AnimGraphNode_ModifyBone_1'])
assert lib.configure_character(bp,mesh,abp)
path='/Game/jiwon/Characters/ChWomanUE5/ABP_ChWoman_FinalMarkers'
if unreal.EditorAssetLibrary.does_asset_exist(path):pp=unreal.load_asset(path)
else:
    factory=unreal.AnimBlueprintFactory();factory.set_editor_property('target_skeleton',mesh.get_editor_property('skeleton'));factory.set_editor_property('parent_class',unreal.AnimInstance)
    pp=unreal.AssetToolsHelpers.get_asset_tools().create_asset('ABP_ChWoman_FinalMarkers','/Game/jiwon/Characters/ChWomanUE5',unreal.AnimBlueprint,factory)
assert lib.configure_final_limb_markers(pp,mesh)
after=json.loads(lib.describe_graph(abp));counts=lambda g:Counter(n['class'] for n in g['nodes'] if n['class'] not in ['AnimGraphNode_ModifyBone','K2Node_Knot'])
assert counts(before)==counts(after)
for asset in [bp,abp,mesh,pp]:assert unreal.EditorAssetLibrary.save_loaded_asset(asset,False)
result={'applied':True,'before_nodes':len(original['nodes']),'after_nodes':len(after['nodes']),'graph':after,'post_process_graph':json.loads(lib.describe_graph(pp)),
        'mesh':json.loads(lib.describe_mesh(mesh)),'removed_fixed_nodes':['AnimGraphNode_ModifyBone_0','AnimGraphNode_ModifyBone_1'],
        'scale_policy':'preserve existing BP component scale 1.1 and authored unweighted IK marker scales; gate deformation scales separately'}
(out/'applied.json').write_text(json.dumps(result,indent=2),encoding='utf8')
