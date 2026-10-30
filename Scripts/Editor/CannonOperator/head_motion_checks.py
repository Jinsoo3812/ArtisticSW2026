"""Per-frame slow aim sweeps: detect single-frame head rotation spikes on both peers."""
import json,math
scripts=OUT.parent.parent/'LiveScripts'
def run(name):
 s={'unreal':unreal,'OUT':OUT,'live_api':live_api,'result':None};exec(compile((scripts/name).read_text(encoding='utf-8-sig'),name,'exec'),s);return s
run('setup_pie.py');boarded=None;began=None;done=False;rows=[];inputs=[]
label=live_api.get('head_check_label','head_motion')
pitches=[0,45,-15,20]
def on_tick(delta):
 global boarded,began,done,inputs
 if done:return
 if boarded is None:
  worlds=unreal.SWAudioAuthoringLibrary.get_audio_pie_worlds()
  if len(worlds)!=2:return
  pcs=[unreal.GameplayStatics.get_player_controller(w,0) for w in worlds]
  if not all(pc and isinstance(pc.get_controlled_pawn(),unreal.BasePlayer) for pc in pcs):return
  run('board_fixture.py');boarded=unreal.GameplayStatics.get_time_seconds(live_api['cannon_test']['worlds'][0]);return
 f=live_api['cannon_test'];now=unreal.GameplayStatics.get_time_seconds(f['worlds'][0])
 if now-boarded<2:return
 if began is None:
  began=now;sub=unreal.get_default_object(unreal.load_class(None,'/Script/Engine.SubsystemBlueprintLibrary'))
  inputs=[sub.call_method('GetLocalPlayerSubSystemFromPlayerController',args=(pc,unreal.EnhancedInputLocalPlayerSubsystem.static_class())) for pc in f['pcs']]
 phase=int((now-began)/10)
 if phase>=len(pitches):
  for pc,inp in zip(f['pcs'],inputs):inp.inject_input_vector_for_action(pc.get_controlled_pawn().get_editor_property('cannon_look_action'),unreal.Vector(),[],[])
  (OUT/(label+'.json')).write_text(json.dumps(rows));done=True;return
 elapsed=(now-began)%10;yaw=-60+120*max(0,min(1,(elapsed-2)/8));pitch=pitches[phase]
 for pc,inp in zip(f['pcs'],inputs):
  c=pc.get_controlled_pawn();rot=c.get_editor_property('base_mesh').get_editor_property('relative_rotation');br=c.get_editor_property('barrel_mesh').get_editor_property('relative_rotation')
  inp.inject_input_vector_for_action(c.get_editor_property('cannon_look_action'),unreal.Vector((yaw-rot.yaw)*.35,(pitch-br.pitch)*.35,0),[],[])
 for peer,w in enumerate(f['worlds']):
  for p in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.BasePlayer):
   if p.get_name() not in f['names']:continue
   mesh=p.get_editor_property('mesh');quats={}
   for bone in ['head','spine_05']:
    q=mesh.get_socket_transform(bone,unreal.RelativeTransformSpace.RTS_ACTOR).rotation;quats[bone]=[q.x,q.y,q.z,q.w]
   rows.append({'peer':peer,'player':p.get_name(),'time':now,'phase':phase,'steady_sweep':elapsed>3,'yaw':yaw,'pitch':pitch,'quats':quats,'hand_error':max(unreal.SWCannonOperatorAuthoringLibrary.get_operator_contact_error(p,i) for i in [2,3]),'foot_error':max(unreal.SWCannonOperatorAuthoringLibrary.get_operator_contact_error(p,i) for i in [0,1])})
result={'head_sweeps':pitches,'seconds':40,'label':label}
