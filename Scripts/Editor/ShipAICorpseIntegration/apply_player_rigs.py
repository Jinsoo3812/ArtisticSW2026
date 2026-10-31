"""Migrate the latest ShipAI BP/ABP in place, retaining their other authoring."""
import json,shutil,traceback
from pathlib import Path
import unreal
ROOT=Path(unreal.Paths.project_dir()).resolve()
OUT=ROOT/'Saved/ShipAICorpseIntegration';OUT.mkdir(parents=True,exist_ok=True)
EXPECTED=json.loads(Path(__file__).with_name('fixed_rig_corrections.json').read_text(encoding='utf8'))
LIB=unreal.SWCharacterRigValidationLibrary
result={'characters':[],'errors':[]}
try:
    for label in ['Man','Woman']:
        bp_path='/Game/Blueprints/Player/BP_Player_'+label
        abp_path='/Game/Anim_Logic/ABP_Player_'+label
        bp=unreal.load_asset(bp_path);abp=unreal.load_asset(abp_path)
        mesh=unreal.load_asset('/Game/jiwon/Characters/Ch'+label+'UE5/SKM_Ch'+label+'_UE5')
        assert bp and abp and mesh and bp.generated_class() and abp.generated_class()
        before=json.loads(LIB.describe_graph(abp))
        (OUT/(label+'_graph_before.json')).write_text(json.dumps(before,indent=2),encoding='utf8')
        for path in [bp_path,abp_path]:
            source=ROOT/'Content'/(path.removeprefix('/Game/')+'.uasset')
            backup=OUT/'Before'/source.relative_to(ROOT);backup.parent.mkdir(parents=True,exist_ok=True)
            if not backup.exists():shutil.copy2(source,backup)
        component=unreal.get_default_object(bp.generated_class()).get_editor_property('mesh')
        transform=component.get_relative_transform().export_text()
        before_mesh=component.get_editor_property('skeletal_mesh_asset').get_path_name()
        fixed={n['name']:n.get('settings') for n in before['nodes'] if n['class']=='AnimGraphNode_ModifyBone'}
        # Refuse to delete a new ShipAI control merely because its class matches.
        if fixed:
            assert fixed==EXPECTED[label],f'{label}: corrections changed; manual inspection required'
            assert LIB.bypass_fixed_bone_corrections(abp,list(fixed))
        assert LIB.configure_character(bp,mesh,abp)
        after=json.loads(LIB.describe_graph(abp))
        old={n['name']:n for n in before['nodes']};new={n['name']:n for n in after['nodes']}
        assert not(set(new)-set(old))
        for name,node in new.items():
            assert node['guid']==old[name]['guid']
            assert node.get('settings')==old[name].get('settings'),name
        removed=[old[name] for name in set(old)-set(new)]
        assert all(n['class'] in ['AnimGraphNode_ModifyBone','K2Node_Knot'] for n in removed)
        component=unreal.get_default_object(bp.generated_class()).get_editor_property('mesh')
        assert component.get_relative_transform().export_text()==transform
        assert component.get_editor_property('skeletal_mesh_asset')==mesh
        assert component.get_editor_property('anim_class')==abp.generated_class()
        for asset in [abp,bp]:assert unreal.EditorAssetLibrary.save_loaded_asset(asset,False)
        result['characters'].append({'label':label,'old_mesh':before_mesh,'mesh':mesh.get_path_name(),
            'preserved_component_transform':transform,'retained_nodes':len(new),
            'removed_fixed_corrections':list(fixed),'retained_node_guid_and_settings_equal':True})
        (OUT/(label+'_graph_after.json')).write_text(json.dumps(after,indent=2),encoding='utf8')
    result['passed']=True
except Exception:
    result['errors'].append(traceback.format_exc());unreal.log_error(result['errors'][-1])
(OUT/'rig_migration.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
assert result.get('passed'),result
