"""Quick visual gate before the full lifecycle suite."""
import json
scripts=OUT.parent.parent/'LiveScripts'
def run(name):
 s={'unreal':unreal,'OUT':OUT,'live_api':live_api,'result':None}
 exec(compile((scripts/name).read_text(encoding='utf-8-sig'),str(scripts/name),'exec'),s)
 return s
assert not unreal.SWAudioAuthoringLibrary.get_audio_pie_worlds()
run('setup_pie.py');phase='loading';started=None;scope=None;done=False

def on_tick(delta):
 global phase,started,scope,done
 if done:return
 if phase=='loading':
  worlds=unreal.SWAudioAuthoringLibrary.get_audio_pie_worlds()
  if len(worlds)!=2:return
  for w in worlds:
   pc=unreal.GameplayStatics.get_player_controller(w,0)
   if not pc or not isinstance(pc.get_controlled_pawn(),unreal.BasePlayer):return
  run('board_fixture.py');phase='settle';started=unreal.GameplayStatics.get_time_seconds(worlds[0]);return
 w=live_api['cannon_test']['worlds'][0]
 if phase=='settle':
  if unreal.GameplayStatics.get_time_seconds(w)-started<3:return
  run('inspect_rework_pose.py');scope=run('capture.py');phase='capture'
 scope['on_tick'](delta)
 if scope.get('done'):
  (OUT/'rework_review_complete.json').write_text(json.dumps({'captured':True}));done=True
result={'requested':'lit visual review and palm contact measurements'}
