import unreal,json
from pathlib import Path
out=Path(unreal.Paths.project_saved_dir())/'ChWomanRigValidation';lib=unreal.SWCharacterRigValidationLibrary
bp=unreal.load_asset('/Game/Blueprints/Player/BP_Player_Woman');abp=unreal.load_asset('/Game/Anim_Logic/ABP_Player_Woman');mesh=unreal.load_asset('/Game/jiwon/Characters/ChWomanUE5/SKM_ChWoman_UE5')
c=unreal.get_default_object(bp.generated_class()).get_editor_property('mesh');graph=json.loads(lib.describe_graph(abp))
pp=unreal.load_asset('/Game/jiwon/Characters/ChWomanUE5/ABP_ChWoman_FinalMarkers')
result={'mesh':c.get_editor_property('skeletal_mesh_asset').get_path_name(),'anim_class':c.get_editor_property('anim_class').get_path_name(),'component_transform':c.get_relative_transform().export_text(),'graph':graph,'post_process':mesh.get_editor_property('post_process_anim_blueprint').get_path_name(),'post_process_graph':json.loads(lib.describe_graph(pp)),'mesh_description':json.loads(lib.describe_mesh(mesh))}
assert c.get_editor_property('skeletal_mesh_asset')==mesh and c.get_editor_property('anim_class')==abp.generated_class()
assert not any(n['class']=='AnimGraphNode_ModifyBone' for n in graph['nodes'])
assert sum(n['class']=='AnimGraphNode_CopyBone' for n in result['post_process_graph']['nodes'])==4
result['passed']=True;(out/'final_asset_audit.json').write_text(json.dumps(result,indent=2),encoding='utf8')
