"""Verify moving-barrel attachment separately from pose reach; never report a clamped hand as contact."""
import json,math
from pathlib import Path
scripts=OUT.parent.parent/'LiveScripts'
steps=[('board','board_fixture.py',2),('policy','presentation_policy.py',.1),('socket_edit','socket_adjustment.py',None),('neutral_capture','capture.py',None),('tracking','network_checks.py',None),('exit','exit_continuity.py',None),('exit_state','record_exit.py',.1)]
if live_api.get('upper_body_capture'):steps.insert(5,('aim_capture','capture.py',None))
index=-1;scope=None;started=0.;done=False

def on_tick(delta):
 global index,scope,started,done
 if done:return
 if scope is not None:
  if 'on_tick' in scope:scope['on_tick'](delta)
  now=unreal.GameplayStatics.get_time_seconds(live_api['cannon_test']['worlds'][0]);wait=steps[index][2]
  if not (scope.get('done',False) if wait is None else now-started>=wait):return
  scope=None
 if index==-1:
  worlds=unreal.SWAudioAuthoringLibrary.get_audio_pie_worlds()
  if len(worlds)!=2:return
  for w in worlds:
   pc=unreal.GameplayStatics.get_player_controller(w,0)
   if not pc or not isinstance(pc.get_controlled_pawn(),unreal.BasePlayer):return
 index+=1
 if index==len(steps):
  samples=json.loads((OUT/'cannon_network_samples.json').read_text());metrics={}
  for sample in samples:
   for peer,world in enumerate(sample['worlds']):
    for player in world['operators']:
     assert abs(player['actual_yaw']-sample['target_yaw'])<1 and abs(player['actual_pitch']-sample['target_pitch'])<1
     assert all(n=='OperatorBarrelVisual' for n in player['contact_parents'].values()),player
     assert max(player['socket_target_errors'].values())<.01,player
     assert max(player['foot_errors'].values())<1.,player
     k=str(peer)+':'+player['name'];m=metrics.setdefault(k,{'max_hand_error_cm':0.,'max_foot_error_cm':0.,'positions':[]})
     m['max_hand_error_cm']=max(m['max_hand_error_cm'],*player['hand_errors'].values());m['max_foot_error_cm']=max(m['max_foot_error_cm'],*player['foot_errors'].values())
     m['positions'].append(player['contact_positions']['hand_l'])
  for m in metrics.values():
   points=m.pop('positions');m['target_motion_cm']=max(math.dist(points[0],p) for p in points);assert m['target_motion_cm']>20,m
  result={'moving_barrel_socket_contract_passed':True,'time_samples':len(samples),'metrics':metrics,'full_pose_reach_passed':all(m['max_hand_error_cm']<1. for m in metrics.values()),'pose_settings_changed':bool(live_api.get('upper_body_capture'))}
  (OUT/'barrel_socket_validation.json').write_text(json.dumps(result,indent=2));done=True;return
 label,file,wait=steps[index];live_api['capture_stage']=label;scope={'unreal':unreal,'OUT':OUT,'live_api':live_api,'result':None}
 exec(compile((scripts/file).read_text(encoding='utf-8-sig'),str(scripts/file),'exec'),scope)
 started=unreal.GameplayStatics.get_time_seconds(live_api['cannon_test']['worlds'][0])
 (OUT/'barrel_validation_progress.json').write_text(json.dumps({'stage':label}))
result={'requested':'actual moving barrel sockets, editable socket propagation, both sexes/peers, aim range and exit','steps':len(steps)}
