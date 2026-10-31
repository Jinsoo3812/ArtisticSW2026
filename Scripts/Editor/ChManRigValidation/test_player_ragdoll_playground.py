"""PIE: execute the actual bound chord handlers, check two 10s respawns and gender switch."""
import unreal,json,time,traceback
from pathlib import Path
ROOT=Path(unreal.Paths.project_dir()).resolve();assert (ROOT/'ArtisticSW2026.uproject').exists()
OUT=Path(globals().get('PLAYGROUND_OUTPUT_DIR',ROOT/'Saved/PlayerRagdollPoseFix'));OUT.mkdir(parents=True,exist_ok=True);RESULT={'events':[],'errors':[]}
LEVEL=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem);EDITOR=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
LIB=unreal.SWCharacterRigValidationLibrary
def save():(OUT/'playground_test.json').write_text(json.dumps(RESULT,indent=2),encoding='utf8')
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
class Test:
    def __init__(self):
        self.original=unreal.SWAudioAuthoringLibrary.configure_audio_pie(1)
        self.state='loading';self.deadline=time.monotonic()+120;self.handle=unreal.register_slate_post_tick_callback(self.tick);LEVEL.editor_request_begin_play()
    def finish(self):save();LEVEL.editor_request_end_play();self.state='ending';self.end=time.monotonic()+2
    def start_death(self):
        self.old=self.pc.get_controlled_pawn();self.start=unreal.GameplayStatics.get_time_seconds(self.world)
        assert self.mode.execute_test_chord(False),'Death chord binding not installed'
        self.saw_physics=False
        self.state='waiting'
    def tick(self,dt):
        try:
            if self.state=='ending':
                if time.monotonic()<self.end or LEVEL.is_in_play_in_editor():return
                unreal.SWAudioAuthoringLibrary.restore_audio_pie(self.original);unreal.unregister_slate_post_tick_callback(self.handle);unreal.EditorPythonScripting.set_keep_python_script_alive(False);unreal.SystemLibrary.quit_editor();return
            assert time.monotonic()<self.deadline,'Playground test timeout'
            if self.state=='loading':
                self.world=EDITOR.get_game_world()
                if not self.world:return
                self.pc=unreal.GameplayStatics.get_player_controller(self.world,0);self.mode=unreal.GameplayStatics.get_game_mode(self.world)
                if not self.pc or not self.pc.get_controlled_pawn():return
                self.start=unreal.GameplayStatics.get_time_seconds(self.world);self.state='alive';self.round=0;return
            now=unreal.GameplayStatics.get_time_seconds(self.world)
            if self.state=='alive':
                pawn=self.pc.get_controlled_pawn();pawn.do_move(0,1)
                if now-self.start<1.2:return
                assert not pawn.get_editor_property('mesh').is_any_simulating_physics()
                self.start_death();return
            if self.state=='waiting':
                elapsed=now-self.start;pawn=self.pc.get_controlled_pawn()
                assert not self.mode.get_editor_property('last_error'),str(self.mode.get_editor_property('last_error'))
                if pawn==self.old:
                    assert elapsed<10.5,'Automatic respawn late'
                    assert json.loads(LIB.snapshot_ragdoll(pawn))['visible'],'Death montage/corpse must remain visible'
                    if pawn.get_editor_property('mesh').is_any_simulating_physics():
                        self.saw_physics=True
                        visual=unreal.SWCorpsePoseLibrary.get_visual(pawn)
                        if elapsed>1:assert visual and visual.is_visible()
                    if elapsed>8:assert self.saw_physics,'Death montage did not transition to a corpse'
                    return
                assert 9.8<=elapsed<=10.5,elapsed
                assert self.saw_physics,'Respawn happened without displaying the corpse'
                snap=json.loads(LIB.snapshot_ragdoll(pawn));assert snap['visible'] and not snap['simulating'] and not snap['health_dead'] and snap['asc_avatar']==snap['actor']
                RESULT['events'].append({'event':'respawn','character':'Man' if self.round==0 else 'Woman','seconds':elapsed,'snapshot':{k:snap[k] for k in ['actor','visible','simulating','health_dead','asc_avatar']}})
                self.round+=1
                if self.round==1:
                    assert self.mode.execute_test_chord(True),'Gender chord binding not installed'
                    pawn=self.pc.get_controlled_pawn();mesh=pawn.get_editor_property('mesh').get_editor_property('skeletal_mesh_asset')
                    assert 'ChWomanUE5' in mesh.get_path_name();RESULT['events'].append({'event':'gender_switch','mesh':mesh.get_path_name()})
                    self.start=now;self.state='alive';return
                assert self.mode.get_editor_property('confirmed_respawns')==3
                RESULT['passed']=True;self.finish()
        except Exception:
            RESULT['errors'].append(traceback.format_exc());unreal.log_error(RESULT['errors'][-1]);self.finish()
RUNNER=Test()
