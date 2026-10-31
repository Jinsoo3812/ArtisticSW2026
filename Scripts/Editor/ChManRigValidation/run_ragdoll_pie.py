"""Actual saved BP/ABP -> physical corpse -> presentation restore in real PIE."""
import unreal,json,time,math,traceback
from pathlib import Path
OUT=Path(unreal.Paths.project_saved_dir())/'PlayerRagdollValidation';OUT.mkdir(parents=True,exist_ok=True)
LIB=unreal.SWCharacterRigValidationLibrary;LEVEL=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem);EDITOR=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem);ACTORS=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
CASES=globals().get('RAGDOLL_CASES',[('Man',60,'idle'),('Woman',60,'idle')]);RESULT={'cases':[],'errors':[]};FLOOR=1000
def magnitude(v):return math.sqrt(sum(x*x for x in v))
def distance(a,b):return magnitude([x-y for x,y in zip(a,b)])
def save():(OUT/globals().get('RAGDOLL_FILENAME','pie_probe.json')).write_text(json.dumps(RESULT,indent=2),encoding='utf8')
def command(s):unreal.SystemLibrary.execute_console_command(EDITOR.get_editor_world(),s)
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
class Runner:
    def __init__(self):
        self.original_pie=unreal.SWAudioAuthoringLibrary.configure_audio_pie(1);self.original_fps=unreal.SystemLibrary.get_console_variable_float_value('t.MaxFPS')
        self.performance=unreal.get_default_object(unreal.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings'));self.throttle=self.performance.get_editor_property('bThrottleCPUWhenNotForeground');self.performance.set_editor_property('bThrottleCPUWhenNotForeground',False)
        fixture=unreal.load_asset('/Game/Developers/ChManRigValidation/BP_RigValidationGameMode');assert LIB.configure_test_game_mode(fixture)
        self.world=unreal.EditorLoadingAndSavingUtils.new_blank_map(False);self.world.get_world_settings().set_editor_property('default_game_mode',fixture.generated_class())
        floor=ACTORS.spawn_actor_from_class(unreal.StaticMeshActor,unreal.Vector(0,0,FLOOR-50));component=floor.get_component_by_class(unreal.StaticMeshComponent);component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'));component.set_collision_profile_name('BlockAll');floor.set_actor_scale3d(unreal.Vector(100,100,1))
        self.floor=floor;self.floor_component=component
        self.state='loading';self.index=-1;self.pawn=None;self.deadline=time.monotonic()+300;self.handle=unreal.register_slate_post_tick_callback(self.tick);LEVEL.editor_request_begin_play()
    def finish(self):
        save();LEVEL.editor_request_end_play();self.state='ending';self.end_at=time.monotonic()+2
    def next_case(self):
        if self.pawn:self.pc.un_possess();self.pawn.destroy_actor()
        self.index+=1
        if self.index>=len(CASES):self.pawn=None;self.finish();return
        label,fps,mode=CASES[self.index];command('t.MaxFPS '+str(fps));self.mode=mode;self.floor.set_actor_location(unreal.Vector(0,0,FLOOR-50),False,False)
        if mode=='platform':self.floor_component.set_mobility(unreal.ComponentMobility.MOVABLE);self.floor_component.set_collision_profile_name('ShipDeck')
        bp=unreal.load_asset('/Game/Blueprints/Player/BP_Player_'+label);self.pawn=LIB.spawn_test_character(self.world,bp,unreal.Vector(0,0,FLOOR+94));assert self.pawn
        if mode!='platform':LIB.set_ragdoll_test_movement_base(self.pawn,self.floor_component,unreal.Vector(0,0,0))
        self.pc.possess(self.pawn);mesh=self.pawn.get_editor_property('mesh');mesh.set_editor_property('visibility_based_anim_tick_option',unreal.VisibilityBasedAnimTickOption.ALWAYS_TICK_POSE_AND_REFRESH_BONES)
        pa=json.loads(LIB.describe_ragdoll_physics(mesh.get_editor_property('skeletal_mesh_asset')));assert len(pa['bodies'])==17
        expected=json.loads((Path(unreal.Paths.project_dir())/'SourceArt/Characters/Physics'/('Ch'+label+'_Physics.json')).read_text(encoding='utf8'))
        desired={j['child']:j['swing1_swing2_twist_deg'] for j in expected['joints']}
        for joint in pa['joints']:
            limits=desired[joint['child']]
            assert all(abs(joint[k]-v)<.001 for k,v in zip(['swing1','swing2','twist'],limits)),'Serialized default constraint profile differs from authoring data'
            assert [joint[k+'_motion'] for k in ['swing1','swing2','twist']]==[1 if v>0 else 2 for v in limits],'Unexpected free angular axis'
        self.limits={j['child']:[j['swing1'],j['swing2'],j['twist']] for j in pa['joints']}
        self.case={'character':label,'requested_fps':fps,'mode':mode,'frames':0,'nonfinite':0,'max_speed_cm_s':0,'max_angular_speed_rad_s':0,'max_anchor_error_cm':0,'max_limit_excess_deg':0,'max_render_body_error_cm':0,'minimum_com_above_floor_cm':9999,'late_max_speed_cm_s':0,'worst_joint':None,'snapshots':[]}
        RESULT['cases'].append(self.case);self.stage='alive';self.start=unreal.GameplayStatics.get_time_seconds(self.world);self.case_start=self.start;self.state='testing';self.did_duplicate=False;self.did_jump=False
        (OUT/'progress.json').write_text(json.dumps({'case':self.index,'character':label,'fps':fps,'mode':mode,'stage':'alive'}))
    def record(self,s,elapsed):
        c=self.case;c['frames']+=1
        for b in s['bodies']:
            c['nonfinite']+=int(not b['finite']);speed=magnitude(b['velocity']);c['max_speed_cm_s']=max(c['max_speed_cm_s'],speed);c['max_angular_speed_rad_s']=max(c['max_angular_speed_rad_s'],magnitude(b['angular_velocity']))
            c['minimum_com_above_floor_cm']=min(c['minimum_com_above_floor_cm'],b['center_of_mass'][2]-FLOOR)
            if elapsed>7:c['late_max_speed_cm_s']=max(c['late_max_speed_cm_s'],magnitude([b['velocity'][0]-(250 if self.mode=='platform' else 0),b['velocity'][1],b['velocity'][2]]))
            if elapsed>.25:c['max_render_body_error_cm']=max(c['max_render_body_error_cm'],distance(b['position'],b['render_bone_position']))
        if elapsed>.25:
            for j in s['joints']:
                c['max_anchor_error_cm']=max(c['max_anchor_error_cm'],j.get('anchor_error_cm',0))
                excess=j['angular_excess_deg']
                if excess>c['max_limit_excess_deg']:c['max_limit_excess_deg']=excess;c['worst_joint']={'child':j['child'],'angles':[j[k] for k in ['swing1','swing2','twist']],'limits':self.limits[j['child']],'elapsed':elapsed}
        if len(c['snapshots'])<3 and elapsed>[.1,1,7.9][len(c['snapshots'])]:c['snapshots'].append(s)
    def tick(self,dt):
        try:
            if self.state=='ending':
                if time.monotonic()<self.end_at or LEVEL.is_in_play_in_editor():return
                unreal.SWAudioAuthoringLibrary.restore_audio_pie(self.original_pie);self.performance.set_editor_property('bThrottleCPUWhenNotForeground',self.throttle);command('t.MaxFPS '+str(self.original_fps));unreal.unregister_slate_post_tick_callback(self.handle);unreal.EditorPythonScripting.set_keep_python_script_alive(False);unreal.SystemLibrary.quit_editor();return
            if time.monotonic()>self.deadline:raise RuntimeError('Ragdoll PIE deadline exceeded')
            if self.state=='loading':
                self.world=EDITOR.get_game_world()
                if not self.world:return
                self.pc=unreal.GameplayStatics.get_player_controller(self.world,0)
                if not self.pc:return
                floors=unreal.GameplayStatics.get_all_actors_of_class(self.world,unreal.StaticMeshActor);assert len(floors)==1
                self.floor=floors[0];self.floor_component=self.floor.get_component_by_class(unreal.StaticMeshComponent)
                RESULT['world']=unreal.SWAudioAuthoringLibrary.describe_audio_world(self.world);assert 'NetMode=0' in RESULT['world']
                if self.pc.get_controlled_pawn():self.pc.get_controlled_pawn().destroy_actor()
                self.next_case();return
            if self.state!='testing':return
            now=unreal.GameplayStatics.get_time_seconds(self.world);elapsed=now-self.start
            if self.mode=='platform':
                self.floor.set_actor_location(unreal.Vector((now-self.case_start)*250,0,FLOOR-50),False,False)
                if self.stage=='alive':LIB.set_ragdoll_test_movement_base(self.pawn,self.floor_component,unreal.Vector(250,0,0))
            if self.stage=='alive':
                if self.mode not in ['idle','platform']:self.pawn.do_move(0,1)
                if self.mode=='sprint':self.pawn.start_sprint()
                if self.mode=='jump' and elapsed>.5 and not self.did_jump:self.pawn.do_jump_start();self.did_jump=True
                if elapsed<1.2:return
                self.case['alive_pose']=json.loads(LIB.snapshot_character(self.pawn));self.pawn.apply_local_death_ragdoll();self.stage='dead';self.start=now
                if self.mode=='platform':self.case['initial_base_momentum']=json.loads(LIB.snapshot_ragdoll(self.pawn))['bodies'][0]['velocity']
                (OUT/'progress.json').write_text(json.dumps({'case':self.index,'stage':'dead'}));return
            if self.stage=='dead':
                s=json.loads(LIB.snapshot_ragdoll(self.pawn));assert s['simulating'] and s['visible'] and len(s['bodies'])==17
                self.record(s,elapsed)
                if elapsed>1 and not self.did_duplicate:
                    before=[b['velocity'] for b in s['bodies']];self.pawn.apply_local_death_ragdoll();after=json.loads(LIB.snapshot_ragdoll(self.pawn));self.case['duplicate_velocity_delta']=max(distance(v,b['velocity']) for v,b in zip(before,after['bodies']));self.did_duplicate=True
                if elapsed<8:return
                self.case['dead_seconds']=elapsed;self.case['effective_fps']=self.case['frames']/elapsed
                self.case['dead_pose']=json.loads(LIB.snapshot_character(self.pawn));self.pawn.reset_local_death_ragdoll();mesh=self.pawn.get_editor_property('mesh')
                self.case['restored_not_simulating']=not mesh.is_any_simulating_physics();self.case['restored_anim_unpaused']=not mesh.get_editor_property('bPauseAnims');assert self.case['restored_not_simulating'] and self.case['restored_anim_unpaused']
                self.pawn.get_movement_component().set_movement_mode(unreal.MovementMode.MOVE_WALKING);self.stage='restored';self.start=now;return
            if self.stage=='restored':
                self.pawn.do_move(0,.5)
                if elapsed<.7:return
                restored=json.loads(LIB.snapshot_character(self.pawn));self.case['restored_speed']=magnitude(restored['velocity']);self.case['restored_nonfinite']=sum(not b['finite'] for b in restored['bones']);save();self.next_case()
        except Exception:
            RESULT['errors'].append(traceback.format_exc());unreal.log_error(RESULT['errors'][-1]);self.finish()
RUNNER=Runner()
