import json
import math
from pathlib import Path
script_root=OUT.parent.parent/'LiveScripts'
steps=[('board','board_fixture.py',2),('initial_pose','inspect_rework_pose.py',.1),('initial_capture','capture.py',None),('presentation_policy','presentation_policy.py',.1),('exit_focus','exit_continuity.py',None),('record_exit','record_exit.py',.1),('remount','board_fixture.py',2),('destroy','remount_checks.py',None),('walk','after_exit_movement.py',None),('ownership','possession_state.py',.1),('board_aim','board_fixture.py',2),('input_state','input_state.py',.1),('ground_policy','ground_policy.py',.1),('socket_edit','socket_adjustment.py',None),('aim','network_checks.py',None),('capture','capture.py',None),('floor','floor_probe.py',.1),('exit_repeat','exit_continuity.py',None),('record_exit_repeat','record_exit.py',.1),('swim','after_exit_swim.py',None)]
index=-1;scope=None;started=None;done=False;progress=[]

def on_tick(delta):
    global index,scope,started,done
    if done:return
    if scope is not None:
        if 'on_tick' in scope:scope['on_tick'](delta)
        now=unreal.GameplayStatics.get_time_seconds(live_api['cannon_test']['worlds'][0])
        wait=steps[index][2]
        if not (scope.get('done',False) if wait is None else now-started>=wait):return
        if steps[index][0].startswith('exit_'):
            previous={};maximum=0.
            for r in json.loads((OUT/'cannon_exit_continuity.json').read_text()):
                if r['stage']=='idle':assert r['movement']=='<MovementMode.MOVE_WALKING: 1>',('Exit lost its deck floor',r['player'],r['movement'])
                assert 'TURN_IN_PLACE' not in r.get('get_thread_safe_state_controller_presentation_state',''),('Stale turn presentation',r['player'],r['stage'])
                key=(r['peer'],r['player']);old=previous.get(key)
                if old:
                    for bone,point in r['points'].items():maximum=max(maximum,math.dist(point,old['points'][bone]))
                previous[key]=r
            assert maximum<15.,('Pose discontinuity; stop before further tests',steps[index][0],maximum)
        if steps[index][0]=='exit_focus':
            (OUT/'cannon_exit_continuity_focus.json').write_bytes((OUT/'cannon_exit_continuity.json').read_bytes())
        progress[-1]['complete']=True
        scope=None
    if index==-1:
        worlds=unreal.SWAudioAuthoringLibrary.get_audio_pie_worlds()
        if len(worlds)!=2:return
        for w in worlds:
            pc=unreal.GameplayStatics.get_player_controller(w,0)
            if not pc or not isinstance(pc.get_controlled_pawn(),unreal.BasePlayer):return
    index+=1
    if index>=len(steps):
        (OUT/'cannon_validation_complete.json').write_text(json.dumps(progress,indent=2));done=True;return
    label,file,wait=steps[index]
    scope={'unreal':unreal,'OUT':OUT,'live_api':live_api,'result':None}
    exec(compile((script_root/file).read_text(encoding='utf-8-sig'),str(script_root/file),'exec'),scope)
    started=unreal.GameplayStatics.get_time_seconds(live_api['cannon_test']['worlds'][0])
    progress.append({'stage':label,'start':started,'complete':False})
    (OUT/'cannon_validation_progress.json').write_text(json.dumps(progress,indent=2))
result={'requested':'aim, per-frame continuity, normal exit, remount, destruction, movement, swim','steps':len(steps)}
