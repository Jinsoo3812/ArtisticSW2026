"""Listen host + client: actual health death payload, both corpse replicas, new possession.

The fixture uses GameModeBase with production controllers/states; full room respawn
policy is separately covered by the native MultiGameMode lifecycle tests.
"""
import unreal,json,time,traceback,math
from pathlib import Path
OUT=Path(globals().get('NETWORK_OUTPUT_DIR',Path(unreal.Paths.project_saved_dir())/'PlayerRagdollValidation'));OUT.mkdir(parents=True,exist_ok=True);LIB=unreal.SWCharacterRigValidationLibrary
LEVEL=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem);EDITOR=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem);ACTORS=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
RESULT={'errors':[],'samples':[],'new_lives':[]};FLOOR=1000
def save():(OUT/'network.json').write_text(json.dumps(RESULT,indent=2),encoding='utf8')
def command(s):unreal.SystemLibrary.execute_console_command(EDITOR.get_editor_world(),s)
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
class Runner:
    def __init__(self):
        self.settings=unreal.SWAudioAuthoringLibrary.configure_audio_pie(2);self.fps=unreal.SystemLibrary.get_console_variable_float_value('t.MaxFPS');command('t.MaxFPS 60')
        self.fixture=unreal.load_asset('/Game/Developers/ChManRigValidation/BP_RigValidationGameMode');self.old_state_class=unreal.get_default_object(self.fixture.generated_class()).get_editor_property('player_state_class')
        assert LIB.configure_ragdoll_test_game_mode(self.fixture)
        world=unreal.EditorLoadingAndSavingUtils.new_blank_map(False);world.get_world_settings().set_editor_property('default_game_mode',self.fixture.generated_class())
        floor=ACTORS.spawn_actor_from_class(unreal.StaticMeshActor,unreal.Vector(0,0,FLOOR-50));c=floor.get_component_by_class(unreal.StaticMeshComponent);c.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'));c.set_collision_profile_name('BlockAll');floor.set_actor_scale3d(unreal.Vector(100,100,1))
        self.stage='loading';self.deadline=time.monotonic()+100;self.handle=unreal.register_slate_post_tick_callback(self.tick);LEVEL.editor_request_begin_play()
    def finish(self):save();LEVEL.editor_request_end_play();self.stage='ending';self.end_at=time.monotonic()+2
    def players(self,world):return unreal.GameplayStatics.get_all_actors_of_class(world,unreal.BasePlayer)
    def row(self,p):
        r=json.loads(LIB.snapshot_ragdoll(p));h=p.get_component_by_class(unreal.BaseHealthComponent)
        impact=h.get_death_ragdoll_impact_data();r['impact_direction']=impact.get_editor_property('bHasDirection');r['impact_bone']=str(impact.get_editor_property('HitBoneName'))
        return r
    def tick(self,dt):
        try:
            if self.stage=='ending':
                if time.monotonic()<self.end_at or LEVEL.is_in_play_in_editor():return
                unreal.get_default_object(self.fixture.generated_class()).set_editor_property('player_state_class',self.old_state_class)
                unreal.SWAudioAuthoringLibrary.restore_audio_pie(self.settings);command('t.MaxFPS '+str(self.fps));unreal.unregister_slate_post_tick_callback(self.handle);unreal.EditorPythonScripting.set_keep_python_script_alive(False);unreal.SystemLibrary.quit_editor();return
            if time.monotonic()>self.deadline:raise RuntimeError('Network ragdoll fixture deadline exceeded')
            if self.stage=='loading':
                worlds=unreal.SWAudioAuthoringLibrary.get_audio_pie_worlds();descriptions=[unreal.SWAudioAuthoringLibrary.describe_audio_world(w) for w in worlds]
                server=next((w for w,d in zip(worlds,descriptions) if 'NetMode=2' in d),None);client=next((w for w,d in zip(worlds,descriptions) if 'NetMode=3' in d),None)
                if not server or not client:return
                pcs=[unreal.GameplayStatics.get_player_controller(server,i) for i in [0,1]]
                if not all(pcs):return
                self.server=server;self.client=client;self.pcs=pcs;RESULT['worlds']=descriptions;self.pawns=[]
                for i,label in enumerate(['Man','Woman']):
                    p=LIB.spawn_test_character(server,unreal.load_asset('/Game/Blueprints/Player/BP_Player_'+label),unreal.Vector(i*350,0,FLOOR+94));pcs[i].possess(p);self.pawns.append(p)
                self.start=unreal.GameplayStatics.get_time_seconds(server);self.stage='alive';return
            now=unreal.GameplayStatics.get_time_seconds(self.server);elapsed=now-self.start
            if self.stage=='alive':
                if elapsed<2 or len(self.players(self.client))!=2:return
                rows=[self.row(p) for w in [self.server,self.client] for p in self.players(w)]
                assert all(not r['health_dead'] and r['asc_avatar']==r['actor'] for r in rows),'Initial life/ASC was not ready'
                RESULT['alive']=[{k:r[k] for k in ['actor','authority','mesh','health_dead','asc_avatar']} for r in rows]
                assert LIB.kill_ragdoll_test_character(self.pawns[0],True)
                assert LIB.kill_ragdoll_test_character(self.pawns[1],False)
                self.start=now;self.stage='dead';self.last_sample=-1;return
            if self.stage=='dead':
                if elapsed<.5:return
                if elapsed-self.last_sample>.5:
                    rows=[self.row(p) for w in [self.server,self.client] for p in self.players(w)]
                    if not(len(rows)==4 and all(r['health_dead'] and r['simulating'] and r['visible'] for r in rows)):
                        assert elapsed<5,'Montage completion/replication did not produce four visible physical corpses'
                        return
                    for world in [self.server,self.client]:
                        for pawn in self.players(world):
                            visual=unreal.SWCorpsePoseLibrary.get_visual(pawn)
                            assert visual and visual.is_visible(),'Replicated physical corpse has no corrected skin'
                    assert all(b['finite'] for r in rows for b in r['bodies'])
                    for row in rows:
                        if 'ChManUE5' in row['mesh']:assert row['impact_direction'] and row['impact_bone']=='index_03_r'
                    RESULT['samples'].append({'elapsed':elapsed,'replicas':rows});self.last_sample=elapsed;save()
                if elapsed<8:return
                self.new=[]
                for i,label in enumerate(['Man','Woman']):
                    p=LIB.spawn_ragdoll_test_new_life(self.pcs[i],unreal.load_asset('/Game/Blueprints/Player/BP_Player_'+label),unreal.Vector(i*350,350,FLOOR+94));assert p;self.new.append(p)
                self.start=now;self.stage='new_life';return
            if self.stage=='new_life':
                if elapsed<2:return
                rows=[self.row(p) for w in [self.server,self.client] for p in self.players(w)]
                living=[r for r in rows if not r['health_dead']];dead=[r for r in rows if r['health_dead']]
                assert len(living)==4 and len(dead)==4
                assert all(not r['simulating'] and r['visible'] and r['asc_avatar']==r['actor'] for r in living),'New life lost visibility or ASC ownership'
                assert all(r['simulating'] and r['visible'] for r in dead),'Old corpse presentation changed on new possession'
                RESULT['new_lives']=[{k:r[k] for k in ['actor','authority','simulating','visible','health_dead','asc_avatar']} for r in rows]
                RESULT['passed']=True;self.finish()
        except Exception:
            RESULT['errors'].append(traceback.format_exc());unreal.log_error(RESULT['errors'][-1]);self.finish()
RUNNER=Runner()
