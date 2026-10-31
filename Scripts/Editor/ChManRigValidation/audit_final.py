"""Read back saved production connections and fail if migration contracts changed."""
import unreal,json
from pathlib import Path
lib=unreal.SWCharacterRigValidationLibrary
out=Path(unreal.Paths.project_saved_dir())/'ChManRigValidation'
bp=unreal.load_asset('/Game/Blueprints/Player/BP_Player_Man')
abp=unreal.load_asset('/Game/Anim_Logic/ABP_Player_Man')
mesh=unreal.load_asset('/Game/jiwon/Characters/ChManUE5/SKM_ChMan_UE5')
cdo=unreal.get_default_object(bp.generated_class());component=cdo.get_editor_property('mesh')
graph=json.loads(lib.describe_graph(abp))
pp=unreal.load_asset('/Game/jiwon/Characters/ChManUE5/ABP_ChMan_FinalMarkers')
result={'mesh':component.get_editor_property('skeletal_mesh_asset').get_path_name(),
        'anim_class':component.get_editor_property('anim_class').get_path_name(),
        'graph':graph,'mesh_description':json.loads(lib.describe_mesh(mesh)),
        'post_process':mesh.get_editor_property('post_process_anim_blueprint').get_path_name(),
        'post_process_graph':json.loads(lib.describe_graph(pp))}
assert component.get_editor_property('skeletal_mesh_asset')==mesh
assert component.get_editor_property('anim_class')==abp.generated_class()
assert not any(n['class']=='AnimGraphNode_ModifyBone' for n in graph['nodes'])
assert sum(n['class']=='AnimGraphNode_CopyBone' for n in result['post_process_graph']['nodes'])==4
result['passed']=True
(out/'final_asset_audit.json').write_text(json.dumps(result,indent=2),encoding='utf8')
