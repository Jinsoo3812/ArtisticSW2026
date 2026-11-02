"""Additional visual review at the combined yaw/pitch limit, without replacing acceptance samples."""
import json
scripts=OUT.parent.parent/'LiveScripts'

def run(name):
 scope={'unreal':unreal,'OUT':OUT,'live_api':live_api,'result':None}
 exec(compile((scripts/name).read_text(encoding='utf-8-sig'),name,'exec'),scope)
 return scope
run('board_fixture.py')
f=live_api['cannon_test'];world=f['worlds'][0];began=None;capture=None;done=False
sub=unreal.get_default_object(unreal.load_class(None,'/Script/Engine.SubsystemBlueprintLibrary'))
inputs=[sub.call_method('GetLocalPlayerSubSystemFromPlayerController',args=(pc,unreal.EnhancedInputLocalPlayerSubsystem.static_class())) for pc in f['pcs']]

def on_tick(delta):
 global began,capture,done
 if done:return
 now=unreal.GameplayStatics.get_time_seconds(world)
 if began is None:began=now
 if now-began<2:return
 for pc,inp in zip(f['pcs'],inputs):
  c=pc.get_controlled_pawn();assert isinstance(c,unreal.Cannon)
  yaw=c.get_editor_property('base_mesh').get_editor_property('relative_rotation').yaw
  pitch=c.get_editor_property('barrel_mesh').get_editor_property('relative_rotation').pitch
  inp.inject_input_vector_for_action(c.get_editor_property('cannon_look_action'),unreal.Vector((60-yaw)*.35,(45-pitch)*.35,0),[],[])
 if now-began<5:return
 if capture is None:
  live_api['capture_stage']='extreme_capture';capture=run('capture.py')
 capture['on_tick'](delta)
 if capture.get('done'):
  rows=[]
  for w in f['worlds']:
   for p in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.BasePlayer):
    if p.get_name() in f['names']:
     rows.append({'player':p.get_name(),'hand_errors':[unreal.SWCannonOperatorAuthoringLibrary.get_operator_contact_error(p,i) for i in [2,3]],'foot_errors':[unreal.SWCannonOperatorAuthoringLibrary.get_operator_contact_error(p,i) for i in [0,1]]})
  (OUT/'cannon_extreme_capture.json').write_text(json.dumps({'yaw':60,'pitch':45,'contacts':rows},indent=2));done=True
result={'review':'additional combined yaw60/pitch45 visual capture'}
