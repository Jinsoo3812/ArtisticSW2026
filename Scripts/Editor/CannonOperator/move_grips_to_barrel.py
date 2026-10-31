"""Move only the operator contact binding to the actual moving barrel; preserve user pose/station edits."""
import json,shutil
from pathlib import Path
assert not unreal.SWAudioAuthoringLibrary.get_audio_pie_worlds()
root=Path(unreal.Paths.project_dir()).resolve()
for relative in ['Blueprints/Ship/Cannon/BP_Cannon','Blueprints/Ship/Cannon/DA_CannonOperatorStation','Blueprints/Ship/Cannon/Mesh/Operator/SM_OperatorCarriageVisual','Blueprints/Ship/Cannon/Mesh/Operator/SM_OperatorBarrelVisual']:
 source=root/'Content'/(relative+'.uasset');backup=root/'Saved/CannonOperatorValidation/BeforeMovingBarrel'/(relative+'.uasset')
 if not backup.exists():backup.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,backup)
bp=unreal.load_asset('/Game/Blueprints/Ship/Cannon/BP_Cannon')
profile=unreal.load_asset('/Game/Blueprints/Ship/Cannon/DA_CannonOperatorStation')
carriage=unreal.load_asset('/Game/Blueprints/Ship/Cannon/Mesh/Operator/SM_OperatorCarriageVisual')
barrel=unreal.load_asset('/Game/Blueprints/Ship/Cannon/Mesh/Operator/SM_OperatorBarrelVisual')
assert all([bp,profile,carriage,barrel])
# Socket-manager widgets can retain the selected object while sockets are removed.
# Close only affected asset editors before mutating their socket arrays.
asset_editors=unreal.get_editor_subsystem(unreal.AssetEditorSubsystem)
for asset in [bp,profile,carriage,barrel]:asset_editors.close_all_editors_for_asset(asset)
assert str(profile.get_editor_property('left_palm_socket'))=='Operator_LeftPalm' and str(profile.get_editor_property('right_palm_socket'))=='Operator_RightPalm'
frames=[];rows=[]
for side,name in [(-1,'Operator_LeftPalm'),(1,'Operator_RightPalm')]:
 old=carriage.find_socket(name)
 if old:
  location=old.get_editor_property('relative_location');rotation=old.get_editor_property('relative_rotation');scale=old.get_editor_property('relative_scale')
  is_old_default=location.distance(unreal.Vector(-52,side*21.5,23))<.01
 else:location=unreal.Vector(-52,side*21.5,23);rotation=unreal.Rotator(yaw=-side*90.);scale=unreal.Vector(1,1,1);is_old_default=True
 if is_old_default:
  location=unreal.Vector(-60,side*14,23)
  rotation=unreal.MathLibrary.make_rot_from_xz(unreal.Vector(0,0,-1),unreal.Vector(0,side,0))
 frames.append(unreal.Transform(location=location,rotation=rotation,scale=scale))
 rows.append({'name':name,'existing_barrel_socket_preserved':bool(barrel.find_socket(name)),'copied_custom_carriage_edit':not is_old_default})
barrel.modify();unreal.SWCannonOperatorAuthoringLibrary.ensure_operator_contact_sockets(barrel,*frames)
for name in ['Operator_LeftPalm','Operator_RightPalm']:
 old=carriage.find_socket(name)
 if old:carriage.modify();carriage.remove_socket(old)
profile.modify();profile.set_editor_property('left_palm',frames[0]);profile.set_editor_property('right_palm',frames[1])
sub=unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem);fn=unreal.SubobjectDataBlueprintFunctionLibrary
found=[];bp.modify()
for h in sub.k2_gather_subobject_data_for_blueprint(bp):
 obj=fn.get_object_for_blueprint(sub.k2_find_subobject_data_from_handle(h),bp)
 if not obj or obj.get_name().removesuffix('_GEN_VARIABLE') not in ['OperatorCarriageVisual','OperatorBarrelVisual']:continue
 name=obj.get_name().removesuffix('_GEN_VARIABLE');obj.modify()
 tag=str(profile.get_editor_property('contact_surface_tag'));assert tag and tag!='None'
 tags=[t for t in obj.get_editor_property('component_tags') if str(t)!=tag]
 if name=='OperatorBarrelVisual':tags.append(tag)
 obj.set_editor_property('component_tags',tags);found.append(name)
assert len(found)==2,found
unreal.BlueprintEditorLibrary.compile_blueprint(bp)
for asset in [barrel,carriage,profile,bp]:assert unreal.EditorAssetLibrary.save_loaded_asset(asset,False),asset.get_path_name()
result={'contact_mesh':barrel.get_path_name(),'old_carriage_sockets_removed':True,'sockets':rows,'pose_and_mount_settings_preserved':True,'affected_asset_editors_closed_before_socket_changes':True}
(OUT/'barrel_socket_migration.json').write_text(json.dumps(result,indent=2))
