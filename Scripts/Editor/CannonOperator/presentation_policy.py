"""Check presentation isolation on the actual mounted actors in both network worlds."""
import json,hashlib
from pathlib import Path
rows=[]
for w in live_api['cannon_test']['worlds']:
 for p in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.BasePlayer):
  if p.get_name() not in live_api['cannon_test']['names']:continue
  c=p.get_attach_parent_actor();assert isinstance(c,unreal.Cannon)
  profile=unreal.get_default_object(c.get_class()).get_editor_property('operator_station_profile');assert profile
  parts={v.get_name():v for v in c.get_components_by_class(unreal.StaticMeshComponent)}
  assert not any(n in parts for n in ['OperatorControlBar','OperatorControlStand'])
  original=c.get_editor_property('barrel_mesh')
  assert original.get_editor_property('static_mesh').get_path_name()=='/Game/Blueprints/Ship/Cannon/Mesh/SM_Cannon.SM_Cannon'
  assert not original.is_visible()
  assert original.get_collision_enabled()==unreal.CollisionEnabled.QUERY_AND_PHYSICS
  for name in ['OperatorCarriageVisual','OperatorBarrelVisual']:
   assert parts[name].is_visible() and parts[name].get_collision_enabled()==unreal.CollisionEnabled.NO_COLLISION
  assert c.get_editor_property('player_mount_point').get_attach_parent().get_name()=='OperatorPivot'
  assert c.get_editor_property('left_hand_grip').get_attach_parent()==parts['OperatorBarrelVisual']
  assert c.get_editor_property('right_hand_grip').get_attach_parent()==parts['OperatorBarrelVisual']
  assert abs(c.get_editor_property('player_mount_point').get_editor_property('relative_location').x+148)<.01
  mesh=p.get_editor_property('mesh');normal_dots={}
  for side,key in [('l','left_hand_grip'),('r','right_hand_grip')]:
   along=(mesh.get_socket_location('middle_01_'+side)-mesh.get_socket_location('hand_'+side)).normal()
   across=mesh.get_socket_location('pinky_01_'+side)-mesh.get_socket_location('index_01_'+side)
   back=along.cross(across).normal()*(-1 if side=='l' else 1)
   normal_dots[side]=back.dot(c.get_editor_property(key).get_up_vector())
   assert normal_dots[side]>.995,normal_dots
  rows.append({'world':w.get_path_name(),'player':p.get_name(),'station':profile.get_path_name(),'palm_normal_dot':normal_dots,'original_collider_preserved':True,'added_visuals_have_no_collision':True,'no_control_bar':True})
source=Path(unreal.Paths.project_dir())/'Content/Blueprints/Ship/Cannon/Mesh/SM_Cannon.uasset'
result={'actors':rows,'original_mesh_sha256':hashlib.sha256(source.read_bytes()).hexdigest()}
(OUT/'cannon_presentation_policy.json').write_text(json.dumps(result,indent=2))
