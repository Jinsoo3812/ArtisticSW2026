import json,shutil
from pathlib import Path
assert not unreal.SWAudioAuthoringLibrary.get_audio_pie_worlds()
root=Path(unreal.Paths.project_dir()).resolve();rows=[]
# Correct only our previous generated stance; preserve custom foot anchors.
station=unreal.load_asset('/Game/Blueprints/Ship/Cannon/DA_CannonOperatorStation')
source=root/'Content/Blueprints/Ship/Cannon/DA_CannonOperatorStation.uasset';backup=root/'Saved/CannonOperatorValidation/BeforeContactSpineFollow/DA_CannonOperatorStation.uasset'
if not backup.exists():backup.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,backup)
for key,old,new in [('left_foot',(12,-13,-92),(42,-28,-92)),('right_foot',(-4,13,-92),(26,28,-92))]:
 point=station.get_editor_property(key)
 if point.distance(unreal.Vector(*old))<.01 or point.distance(unreal.Vector(42 if key=='left_foot' else 26,-18 if key=='left_foot' else 18,-92))<.01:station.modify();station.set_editor_property(key,unreal.Vector(*new))
assert unreal.EditorAssetLibrary.save_loaded_asset(station,False)
for sex in ['Man','Woman']:
 source=root/('Content/Anim_Logic/ABP_Player_'+sex+'.uasset');backup=root/('Saved/CannonOperatorValidation/BeforeContactSpineFollow/ABP_Player_'+sex+'.uasset')
 if not backup.exists():backup.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,backup)
 bp=unreal.load_asset('/Game/Anim_Logic/ABP_Player_'+sex)
 unreal.get_editor_subsystem(unreal.AssetEditorSubsystem).close_all_editors_for_asset(bp)
 for node in unreal.ObjectIterator(unreal.AnimGraphNode_SWCannonOperator):
  if node.get_path_name().startswith(bp.get_path_name().split('.')[0]+'.'):
   node.modify();pose=node.get_editor_property('node')
   values={'blend_out_speed':1.5,'knee_width':36.,'max_torso_yaw':25.,'max_torso_pitch':25.,'torso_yaw_follow':.35,'max_side_lean':45.,'max_head_aim_correction':80.,'extra_waist_bend':6.,'turn_waist_bend':8.,'max_waist_bend':70.,'max_pelvis_forward_shift':35.,'max_pelvis_side_shift':30.,'max_reach_crouch':40.,'shoulder_above_grip':48.,'grip_look_weight':.25}
   if sex=='Woman' and abs(pose.get_editor_property('forward_lean')-25)<.01:values['forward_lean']=15.
   for key,value in values.items():pose.set_editor_property(key,value)
   node.set_editor_property('node',pose);rows.append({'sex':sex,'settings':values})
 bp.modify();unreal.BlueprintEditorLibrary.compile_blueprint(bp);assert unreal.EditorAssetLibrary.save_loaded_asset(bp,False)
(OUT/'upper_body_settings.json').write_text(json.dumps(rows,indent=2))
live_api['upper_body_capture']=True
live_api['cannon_validation_angles']=[(0,0),(-60,-15),(-60,45),(0,45),(60,45),(60,-15),(0,-15),(60,0)]*2
exec(compile((OUT.parent.parent/'LiveScripts/run_validation.py').read_text(encoding='utf-8-sig').replace("run('validation_sequence.py')","run('barrel_validation_sequence.py')"),'upper-body-validation','exec'),globals())
