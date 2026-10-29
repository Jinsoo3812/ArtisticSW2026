"""Verify NPC-owned sash references and preservation of the original skeleton."""
import json,math
from pathlib import Path
import unreal
ROOT=Path(unreal.Paths.project_dir());ART=ROOT/'SourceArt/Characters/YiSunSin';BASE='/Game/Chae_Asset/YiSunSinContent';NPC=BASE+'/NPC'
source=unreal.load_asset(BASE+'/Characters/SK_YiSunSin_Latest');mesh=unreal.load_asset(NPC+'/Characters/SK_YiSunSin_NPC_Sash')
skeleton=mesh.get_editor_property('skeleton');original=source.get_editor_property('skeleton')
assert skeleton!=original
c=unreal.SkeletalMeshComponent();c.set_skeletal_mesh_asset(mesh)
s=unreal.SkeletalMeshComponent();s.set_skeletal_mesh_asset(source)
assert c.get_num_bones()==87 and s.get_num_bones()==83
max_translation=0;min_quat_dot=1.0
for i in range(83):
 name=s.get_bone_name(i);index=c.get_bone_index(name);assert index>=0
 assert c.get_parent_bone(name)==s.get_parent_bone(name)
 a=c.get_ref_pose_transform(index);b=s.get_ref_pose_transform(i)
 max_translation=max(max_translation,(a.translation-b.translation).length())
 dot=abs(a.rotation.x*b.rotation.x+a.rotation.y*b.rotation.y+a.rotation.z*b.rotation.z+a.rotation.w*b.rotation.w)
 min_quat_dot=min(min_quat_dot,dot)
assert max_translation<.002 and min_quat_dot>.99999
blend=unreal.load_asset(NPC+'/Animations/BS_YiSunSin_NPC_Sash');abp=unreal.load_asset(NPC+'/Animations/ABP_YiSunSin_NPC_Sash');bp=unreal.load_asset(NPC+'/Blueprints/BP_YiSunSin_NPC')
assert blend.get_editor_property('skeleton')==skeleton and abp.get_editor_property('target_skeleton')==skeleton
samples=blend.get_editor_property('sample_data');assert [x.get_editor_property('sample_value').x for x in samples]==[0,150,450]
for sample in samples:assert sample.get_editor_property('animation').get_editor_property('skeleton')==skeleton
unreal.BlueprintEditorLibrary.compile_blueprint(abp);unreal.BlueprintEditorLibrary.compile_blueprint(bp)
component=unreal.get_default_object(bp.generated_class()).get_editor_property('mesh')
assert component.get_editor_property('skeletal_mesh')==mesh and component.get_editor_property('anim_class')==abp.generated_class()
# Compare original body animation; extra sash bones may vary, original tracks may not.
body_error=0;root_scale_error=0;body_rotation_error=0
for label in ['Walk','Run']:
 old=unreal.load_asset(BASE+'/Animations/Latest/AS_YiSunSin_'+label)
 new=unreal.load_asset(NPC+'/Animations/AS_YiSunSin_NPC_Sash_'+label)
 assert abs(old.get_play_length()-new.get_play_length())<.001
 for phase in [0,.25,.5,.75,.99]:
  t=phase*old.get_play_length()
  a=unreal.AnimPoseExtensions.get_anim_pose_at_time(old,t,unreal.AnimPoseEvaluationOptions())
  b=unreal.AnimPoseExtensions.get_anim_pose_at_time(new,t,unreal.AnimPoseEvaluationOptions())
  for i in range(83):
   name=s.get_bone_name(i)
   x=unreal.AnimPoseExtensions.get_bone_pose(a,name,unreal.AnimPoseSpaces.LOCAL);y=unreal.AnimPoseExtensions.get_bone_pose(b,name,unreal.AnimPoseSpaces.LOCAL)
   body_error=max(body_error,(x.translation-y.translation).length())
   dot=min(1,abs(x.rotation.x*y.rotation.x+x.rotation.y*y.rotation.y+x.rotation.z*y.rotation.z+x.rotation.w*y.rotation.w))
   body_rotation_error=max(body_rotation_error,2*math.degrees(math.acos(dot)))
  root=unreal.AnimPoseExtensions.get_bone_pose(b,'spine',unreal.AnimPoseSpaces.LOCAL)
  root_scale_error=max(root_scale_error,abs(root.scale3d.x-100))
assert body_error<.01,(body_error,body_rotation_error)
assert body_rotation_error<.2 and root_scale_error<.01,(body_error,body_rotation_error,root_scale_error)
report={'passed':True,'original_skeleton_unchanged':original.get_path_name(),'npc_skeleton':skeleton.get_path_name(),
 'original_bone_count':83,'npc_bone_count':87,'bind_translation_error_cm':max_translation,
 'body_animation_translation_error_cm':body_error,'body_animation_rotation_error_degrees':body_rotation_error,
 'root_scale_error':root_scale_error,'npc_bp_uses_sash_variant':True,'speeds':[0,150,450]}
(ART/'Audit/sash_saved_assets_validation.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
unreal.log('NPC_SASH_SAVED_ASSETS_VERIFIED')
