"""Author only cannon presentation and the final operator overlay. Preserve gameplay components."""
import json,shutil
from pathlib import Path
root=Path(unreal.Paths.project_dir()).resolve()
assert not unreal.SWAudioAuthoringLibrary.get_audio_pie_worlds(),'End PIE before authoring'
for asset in ['Blueprints/Ship/Cannon/BP_Cannon','Anim_Logic/ABP_Player_Man','Anim_Logic/ABP_Player_Woman']:
 source=root/'Content'/(asset+'.uasset');backup=root/'Saved/CannonOperatorValidation/BeforeRework'/(asset+'.uasset')
 if not backup.exists():backup.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,backup)
bp=unreal.load_asset('/Game/Blueprints/Ship/Cannon/BP_Cannon')
sub=unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem);fn=unreal.SubobjectDataBlueprintFunctionLibrary

def collect():
 hs=sub.k2_gather_subobject_data_for_blueprint(bp)
 return hs,{o.get_name().removesuffix('_GEN_VARIABLE'):(h,o) for h in hs if (o:=fn.get_object_for_blueprint(sub.k2_find_subobject_data_from_handle(h),bp))}

hs,parts=collect()
for name in ['OperatorControlBar','OperatorControlStand']:
 if name in parts:assert sub.delete_subobject(hs[0],parts[name][0],bp)==1
hs,parts=collect()
assert not any(n in parts for n in ['OperatorControlBar','OperatorControlStand'])
source=unreal.load_asset('/Game/Blueprints/Ship/Cannon/Mesh/SM_Cannon')
for name,parent,barrel in [('OperatorCarriageVisual','OperatorPivot',False),('OperatorBarrelVisual','BarrelMesh',True)]:
 mesh=unreal.SWCannonOperatorAuthoringLibrary.build_operator_cannon_part(source,'/Game/Blueprints/Ship/Cannon/Mesh/Operator/SM_'+name,barrel)
 assert mesh,name
 assert unreal.EditorAssetLibrary.save_loaded_asset(mesh,False)
 if name not in parts:
  h,failure=sub.add_new_subobject(unreal.AddNewSubobjectParams(parent_handle=parts[parent][0],new_class=unreal.StaticMeshComponent,blueprint_context=bp))
  assert fn.is_handle_valid(h),str(failure)
  sub.rename_subobject(h,unreal.Text(name));hs,parts=collect()
 c=parts[name][1];c.set_static_mesh(mesh);c.set_visibility(True,False)
 tags=[t for t in c.get_editor_property('component_tags') if str(t)!='CannonOperatorContact']
 if barrel:tags.append('CannonOperatorContact')
 c.set_editor_property('component_tags',tags)
 c.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION);c.set_editor_property('generate_overlap_events',False)
 c.set_editor_property('relative_location',unreal.Vector());c.set_editor_property('relative_rotation',unreal.Rotator());c.set_editor_property('relative_scale3d',unreal.Vector(1,1,1))
# Keep the original full-mesh collider, yaw/pitch parent, muzzle, camera and all firing properties.
parts['BarrelMesh'][1].set_visibility(False,False)
for name in ['PlayerMountPoint','PlayerExitPoint']:
 c=parts[name][1];point=c.get_editor_property('relative_location');point.x=-148.;c.set_editor_property('relative_location',point)
for side,name in [(-1,'LeftHandGrip'),(1,'RightHandGrip')]:
 c=parts[name][1];c.set_editor_property('relative_location',unreal.Vector(-60.,side*14.,23.))
 c.set_editor_property('relative_rotation',unreal.MathLibrary.make_rot_from_xz(unreal.Vector(0,0,-1),unreal.Vector(0,side,0)))
for side,name in [(-1,'LeftFootPlant'),(1,'RightFootPlant')]:
 parts[name][1].set_editor_property('relative_location',unreal.Vector(42 if side<0 else 26,side*28,-92.))
profile_path='/Game/Blueprints/Ship/Cannon/DA_CannonOperatorStation'
profile=unreal.load_asset(profile_path)
if not profile:
 factory=unreal.DataAssetFactory();factory.set_editor_property('data_asset_class',unreal.CannonOperatorStationProfile)
 profile=unreal.AssetToolsHelpers.get_asset_tools().create_asset('DA_CannonOperatorStation','/Game/Blueprints/Ship/Cannon',unreal.CannonOperatorStationProfile,factory)
assert profile
profile.modify()
for key,part in [('mount','PlayerMountPoint'),('exit','PlayerExitPoint'),('left_palm','LeftHandGrip'),('right_palm','RightHandGrip')]:profile.set_editor_property(key,parts[part][1].get_relative_transform())
for key,part in [('left_foot','LeftFootPlant'),('right_foot','RightFootPlant')]:profile.set_editor_property(key,parts[part][1].get_editor_property('relative_location'))
assert unreal.EditorAssetLibrary.save_loaded_asset(profile,False)
contact_mesh=parts['OperatorBarrelVisual'][1].get_editor_property('static_mesh')
unreal.SWCannonOperatorAuthoringLibrary.ensure_operator_contact_sockets(contact_mesh,profile.get_editor_property('left_palm'),profile.get_editor_property('right_palm'))
assert unreal.EditorAssetLibrary.save_loaded_asset(contact_mesh,False)
unreal.get_default_object(bp.generated_class()).set_editor_property('operator_station_profile',profile)
bp.modify();unreal.BlueprintEditorLibrary.compile_blueprint(bp);assert unreal.EditorAssetLibrary.save_loaded_asset(bp,False)
profiles={'Man':dict(pelvis_drop=25.,hip_lean=32.,hip_forward=36.,forward_lean=18.,finger_curl=45.,knee_width=36.,max_torso_yaw=25.,max_torso_pitch=25.,torso_yaw_follow=.35,max_side_lean=45.,max_head_aim_correction=80.,extra_waist_bend=6.,turn_waist_bend=8.,max_waist_bend=70.,max_pelvis_forward_shift=35.,max_pelvis_side_shift=30.,max_reach_crouch=40.,shoulder_above_grip=48.,grip_look_weight=.25,blend_out_speed=1.5),'Woman':dict(pelvis_drop=25.,hip_lean=35.,hip_forward=40.,forward_lean=15.,finger_curl=45.,knee_width=36.,max_torso_yaw=25.,max_torso_pitch=25.,torso_yaw_follow=.35,max_side_lean=45.,max_head_aim_correction=80.,extra_waist_bend=6.,turn_waist_bend=8.,max_waist_bend=70.,max_pelvis_forward_shift=35.,max_pelvis_side_shift=30.,max_reach_crouch=40.,shoulder_above_grip=48.,grip_look_weight=.25,blend_out_speed=1.5)}
for sex,values in profiles.items():
 a=unreal.load_asset('/Game/Anim_Logic/ABP_Player_'+sex)
 found=0
 for node in unreal.ObjectIterator(unreal.AnimGraphNode_SWCannonOperator):
  if node.get_path_name().startswith(a.get_path_name().split('.')[0]+'.'):
   pose=node.get_editor_property('node')
   for key,value in values.items():pose.set_editor_property(key,value)
   node.set_editor_property('node',pose);found+=1
 assert found==1,(sex,found)
 a.modify();unreal.BlueprintEditorLibrary.compile_blueprint(a);assert unreal.EditorAssetLibrary.save_loaded_asset(a,False)
result={'profiles':profiles,'removed':['OperatorControlBar','OperatorControlStand'],'grip':'moving barrel sockets','source_preserved':source.get_path_name()}
(OUT/'cannon_rework_authored.json').write_text(json.dumps(result,indent=2))
