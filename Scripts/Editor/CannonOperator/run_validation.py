"""Run native checks then the complete PIE sequence via the existing live bridge."""
import json,re,time
from pathlib import Path
scripts=OUT.parent.parent/'LiveScripts'
assert not unreal.SWAudioAuthoringLibrary.get_audio_pie_worlds(),'Start validation outside PIE'
match=re.search(r'-abslog=(?:"([^"]+)"|(\S+))',unreal.SystemLibrary.get_command_line(),re.I)
assert match,'Launch the validation editor with -abslog=<validation log>'
log_path=Path(match.group(1) or match.group(2));offset=log_path.stat().st_size
started=time.monotonic();last_poll=0.;phase='unit';scope=None;done=False
unreal.SystemLibrary.execute_console_command(live_api['EDITOR'].get_editor_world(),'Automation RunTests ArtisticSW.Cannon.Operator+ArtisticSW.Animation.JumpAir+ArtisticSW.Animation.Stop+ArtisticSW.GAS.Roll+ArtisticSW.GAS.HitReaction+ArtisticSW.GAS.Strength+ArtisticSW.PlayerBow')

def run(file):
    state={'unreal':unreal,'OUT':OUT,'live_api':live_api,'result':None}
    exec(compile((scripts/file).read_text(encoding='utf-8-sig'),str(scripts/file),'exec'),state)
    return state

def on_tick(delta):
    global last_poll,phase,scope,done
    if done:return
    if phase=='unit':
        now=time.monotonic()
        if now-last_poll<.25:return
        last_poll=now
        assert now-started<120,'Native automation did not finish'
        with log_path.open('rb') as stream:
            stream.seek(max(offset,log_path.stat().st_size-64000));tail=stream.read().decode('utf-8',errors='replace')
        if 'Automation Test Queue Empty' not in tail:return
        if 'Test Completed. Result={Fail}' in tail:raise AssertionError('Native regression test failed; inspect the log')
        assert 'Test Completed. Result={Success}' in tail,'No native test succeeded'
        run('setup_pie.py');scope=run('validation_sequence.py');phase='PIE'
        (OUT/'cannon_unit_passed.json').write_text(json.dumps({'success':True,'log':str(log_path)}))
    else:
        scope['on_tick'](delta)
        if scope.get('done'):
            status={'native_test':True,'PIE_sequence':True}
            if (OUT/'barrel_socket_validation.json').exists():
                pose=json.loads((OUT/'barrel_socket_validation.json').read_text())
                status.update(moving_barrel_socket_contract_passed=pose['moving_barrel_socket_contract_passed'],full_pose_reach_passed=pose['full_pose_reach_passed'])
            (OUT/'cannon_run_complete.json').write_text(json.dumps(status));done=True
result={'requested':'native automation and full PIE sequence','log':str(log_path)}
