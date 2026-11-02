import json
f=live_api['cannon_test'];w=f['worlds'][0];rows=[]
for p in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.BasePlayer):
 if p.get_name() not in f['names']:continue
 c=p.get_attach_parent_actor();mesh=p.get_editor_property('mesh');tf=c.get_actor_transform()
 points={b:str(unreal.MathLibrary.inverse_transform_location(tf,mesh.get_socket_location(b))) for b in ['pelvis','spine_03','head','upperarm_l','lowerarm_l','hand_l','hand_r','thigh_l','calf_l','foot_l','foot_r']}
 errs={}
 for side,name in [('l','left_hand_grip'),('r','right_hand_grip')]:
  hand=mesh.get_socket_location('hand_'+side)
  palm=hand+sum((mesh.get_socket_location(s+'_01_'+side)-hand for s in ['index','middle','ring','pinky']),unreal.Vector())*.1375
  errs[side]=unreal.SWCannonOperatorAuthoringLibrary.get_operator_contact_error(p,2 if side=='l' else 3)
 rows.append({'name':p.get_name(),'points':points,'palm_errors':errs})
(OUT/'rework_pose_points.json').write_text(json.dumps(rows,indent=2));result=rows
assert all(max(r['palm_errors'].values())<1. for r in rows),rows
