"""Real PIE comparison: production BP, actual MM graph, fixed-offset ablation.

No production BP/ABP saves. Raw transforms are reduced every evaluated frame;
only phase snapshots and bounded outlier records are written.
"""
import unreal,json,time,math,traceback
from pathlib import Path
OUT=Path(globals().get('OUTPUT_DIR',Path(unreal.Paths.project_saved_dir())/'ChManRigValidation'));OUT.mkdir(parents=True,exist_ok=True)
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
LEVEL=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
EDITOR=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
ACTORS=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
LIB=unreal.SWCharacterRigValidationLibrary
PHASES=[('idle',2,0,0,False,False,0),('walk',3,0,.35,False,False,0),('run',3,0,1,False,False,0),('stop',1.5,0,0,False,False,0),('strafe_right',2,1,0,False,False,0),('backward',2,0,-1,False,False,0),('turn_in_place',2,0,0,False,False,150),('sprint',3,0,1,True,False,150),('jump_forward',2,0,1,False,True,150),('land_stop',2,0,0,False,False,150),('jump_side',2,1,0,False,True,240),('final_idle',2,0,0,False,False,240)]
CASES=[('old_mesh_offsets',60,'/Game/jiwon/Characters/SKM_Player_Man','/Game/Anim_Logic/ABP_Player_Man'),('new_mesh_offsets',60,'/Game/jiwon/Characters/ChManUE5/SKM_ChMan_UE5','/Game/Anim_Logic/ABP_Player_Man'),('new_mesh_clean',60,'/Game/jiwon/Characters/ChManUE5/SKM_ChMan_UE5','/Game/Developers/ChManRigValidation/ABP_Man_NoFixedRigOffsets'),('new_mesh_clean_30',30,'/Game/jiwon/Characters/ChManUE5/SKM_ChMan_UE5','/Game/Developers/ChManRigValidation/ABP_Man_NoFixedRigOffsets'),('new_mesh_clean_120',120,'/Game/jiwon/Characters/ChManUE5/SKM_ChMan_UE5','/Game/Developers/ChManRigValidation/ABP_Man_NoFixedRigOffsets')]
SAVED_CONNECTION_MODE=globals().get('SAVED_CONNECTION_MODE',False)
if SAVED_CONNECTION_MODE:CASES=globals().get('SAVED_CASES',[('saved_bp_60',60,None,None),('saved_bp_120',120,None,None)])
RESULT_FILENAME=globals().get('RESULT_FILENAME','saved_connection.json' if SAVED_CONNECTION_MODE else 'pie_comparison.json')
RESULT={'cases':[],'errors':[],'source':'real PIE, native BP/ABP, actual selected motion-matching result','phase_snapshots':[]}
RESULT['saved_connection_mode']=SAVED_CONNECTION_MODE
FLOOR_Z=globals().get('FLOOR_Z',0)
RESULT['floor_top_z']=FLOOR_Z
def length(v):return math.sqrt(sum(x*x for x in v))
def distance(a,b):return length([x-y for x,y in zip(a,b)])
def angle(a,b):return math.degrees(2*math.acos(min(1,abs(sum(x*y for x,y in zip(a,b))))))
def save():
    (OUT/RESULT_FILENAME).write_text(json.dumps(RESULT,indent=2),encoding='utf8')
def execute(command):unreal.SystemLibrary.execute_console_command(EDITOR.get_editor_world(),command)

class Runner:
    def __init__(self):
        self.started=time.monotonic();self.state='setup';self.case_index=-1;self.phase_index=-1;self.pawn=None;self.previous=None;self.phase_snapshot=False
        self.original_fps=unreal.SystemLibrary.get_console_variable_float_value('t.MaxFPS')
        self.original_pie=unreal.SWAudioAuthoringLibrary.configure_audio_pie(1)
        (OUT/'pie_settings_before.json').write_text(self.original_pie,encoding='utf8')
        self.performance=unreal.get_default_object(unreal.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings'))
        self.original_throttle=self.performance.get_editor_property('bThrottleCPUWhenNotForeground')
        self.performance.set_editor_property('bThrottleCPUWhenNotForeground',False)
        fixture_path='/Game/Developers/ChManRigValidation/BP_RigValidationGameMode'
        fixture=unreal.load_asset(fixture_path);assert fixture,'Author the fixture explicitly before running a read-only comparison'
        assert LIB.configure_test_game_mode(fixture)
        self.fixture_class=fixture.generated_class()
        self.mode=unreal.get_default_object(self.fixture_class)
        self.original_controller=self.mode.get_editor_property('player_controller_class');self.original_pawn=self.mode.get_editor_property('default_pawn_class')
        self.mode.set_editor_property('player_controller_class',unreal.load_class(None,'/Game/Blueprints/Player/BP_BasePlayerController.BP_BasePlayerController_C'))
        self.mode.set_editor_property('default_pawn_class',None)
        world=unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
        world.get_world_settings().set_editor_property('default_game_mode',self.fixture_class)
        floor=ACTORS.spawn_actor_from_class(unreal.StaticMeshActor,unreal.Vector(0,0,FLOOR_Z-50))
        comp=floor.get_component_by_class(unreal.StaticMeshComponent)
        comp.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'));comp.set_collision_profile_name('BlockAll')
        floor.set_actor_scale3d(unreal.Vector(100,100,1))
        self.bp=unreal.load_asset(globals().get('BP_PATH','/Game/Blueprints/Player/BP_Player_Man'))
        self.handle=unreal.register_slate_post_tick_callback(self.tick)
        LEVEL.editor_request_begin_play();self.state='loading'
    def fail(self):
        RESULT['errors'].append(traceback.format_exc());unreal.log_error(RESULT['errors'][-1]);self.finish()
    def finish(self):
        save()
        if self.pawn:self.pawn.stop_move_input();self.pawn.stop_sprint()
        LEVEL.editor_request_end_play();self.state='ending';self.end_at=time.monotonic()+2
    def next_case(self):
        if self.pawn:
            self.pc.un_possess();self.pawn.destroy_actor()
        self.case_index+=1
        if self.case_index>=len(CASES):self.pawn=None;self.finish();return
        name,fps,mesh_path,anim_path=CASES[self.case_index]
        execute('t.MaxFPS '+str(fps))
        self.pawn=LIB.spawn_test_character(self.world,self.bp,unreal.Vector(0,0,FLOOR_Z+94))
        assert self.pawn,'PIE-only spawn failed'
        self.pc.possess(self.pawn);self.pc.set_control_rotation(unreal.Rotator(0,0,0))
        mesh=self.pawn.get_editor_property('mesh')
        if mesh_path:
            mesh.set_skeletal_mesh_asset(unreal.load_asset(mesh_path));mesh.set_anim_instance_class(unreal.load_asset(anim_path).generated_class())
        else:
            assert mesh.get_editor_property('skeletal_mesh_asset').get_path_name()==globals().get('EXPECTED_MESH','/Game/jiwon/Characters/ChManUE5/SKM_ChMan_UE5.SKM_ChMan_UE5')
            assert mesh.get_editor_property('anim_class').get_path_name()==globals().get('EXPECTED_ABP','/Game/Anim_Logic/ABP_Player_Man.ABP_Player_Man_C')
        mesh.set_editor_property('visibility_based_anim_tick_option',unreal.VisibilityBasedAnimTickOption.ALWAYS_TICK_POSE_AND_REFRESH_BONES)
        self.case={'name':name,'requested_fps':fps,'frames':0,'sampled_seconds':0,'min_dt':999,'max_dt':0,'phase_stats':{},'nonfinite':0,'max_radius_cm':0,'max_scale_error':0,'max_limb_length_error_cm':0,'max_local_rotation_step_deg':0,'max_deform_rotation_step_deg':0,'max_component_rotation_step_deg':0,'max_ik_component_rotation_step_deg':0,'max_rotation_speed_deg_s':0,'outliers':[],'databases':{},'animations':{},'helper_errors':{},'helper_rotation_errors':{}}
        RESULT['cases'].append(self.case);self.phase_index=-1;self.previous=None;self.next_phase()
    def next_phase(self):
        self.phase_index+=1
        if self.phase_index>=len(PHASES):self.next_case();return
        self.phase=PHASES[self.phase_index];name,duration,right,forward,sprint,jump,yaw=self.phase
        self.phase_started=unreal.GameplayStatics.get_time_seconds(self.world);self.phase_snapshot=False
        self.pc.set_control_rotation(unreal.Rotator(0,yaw,0))
        if sprint:self.pawn.start_sprint()
        else:self.pawn.stop_sprint()
        if jump:self.pawn.do_jump_start()
        else:self.pawn.do_jump_end()
        self.case['phase_stats'][name]={'frames':0,'active_mm_frames':0,'invalid_active_mm_frames':0,'max_speed':0,'air_frames':0,'max_limb_length_error_cm':0}
        (OUT/'progress.json').write_text(json.dumps({'case':self.case['name'],'phase':name,'case_index':self.case_index,'total_cases':len(CASES)}))
    def record(self,snapshot):
        phase=self.case['phase_stats'][self.phase[0]];phase['frames']+=1;self.case['frames']+=1
        phase['max_speed']=max(phase['max_speed'],length(snapshot['velocity']))
        if snapshot['movement_mode']==3:phase['air_frames']+=1
        active=[n for n in snapshot['motion_matching'] if n['weight']>.01]
        if active:phase['active_mm_frames']+=1
        if any(not n['valid'] for n in active):phase['invalid_active_mm_frames']+=1
        for n in active:
            if n.get('database'):self.case['databases'][n['database']]=self.case['databases'].get(n['database'],0)+1
            if n.get('animation'):self.case['animations'][n['animation']]=self.case['animations'].get(n['animation'],0)+1
        previous={b['name']:b for b in self.previous['bones']} if self.previous else {}
        dt=snapshot['time']-self.previous['time'] if self.previous else 0
        if dt>0:
            self.case['sampled_seconds']+=dt;self.case['min_dt']=min(self.case['min_dt'],dt);self.case['max_dt']=max(self.case['max_dt'],dt)
        root=snapshot['bones'][0]['position']
        by_name={b['name']:b for b in snapshot['bones']}
        for marker,limb in [('ik_hand_l','hand_l'),('ik_hand_r','hand_r'),('ik_foot_l','foot_l'),('ik_foot_r','foot_r')]:
            if marker in by_name and limb in by_name:
                error=distance(by_name[marker]['position'],by_name[limb]['position'])
                self.case['helper_errors'][marker]=max(self.case['helper_errors'].get(marker,0),error)
                error=angle(by_name[marker]['rotation'],by_name[limb]['rotation'])
                self.case['helper_rotation_errors'][marker]=max(self.case['helper_rotation_errors'].get(marker,0),error)
        for b in snapshot['bones']:
            n=b['name'];self.case['nonfinite']+=int(not b['finite'])
            self.case['max_radius_cm']=max(self.case['max_radius_cm'],distance(b['position'],root))
            scale_error=max(abs(x-1) for x in b['local']['scale'])
            if scale_error>self.case['max_scale_error']:
                self.case['max_scale_error']=scale_error;self.case['max_scale_detail']={'bone':n,'phase':self.phase[0],'scale':b['local']['scale']}
            if not n.startswith('ik_'):
                self.case['max_deform_scale_error']=max(self.case.get('max_deform_scale_error',0),scale_error)
            if n in ['lowerarm_l','lowerarm_r','hand_l','hand_r','calf_l','calf_r','foot_l','foot_r']:
                error=abs(length(b['local']['position'])-b['bind_length'])
                self.case['max_limb_length_error_cm']=max(self.case['max_limb_length_error_cm'],error);phase['max_limb_length_error_cm']=max(phase['max_limb_length_error_cm'],error)
            if n in previous and dt>0:
                step=angle(b['local']['rotation'],previous[n]['local']['rotation'])
                component_step=angle(b['rotation'],previous[n]['rotation'])
                if component_step>self.case['max_component_rotation_step_deg']:
                    self.case['max_component_rotation_step_deg']=component_step
                    self.case['max_component_rotation_detail']={'bone':n,'phase':self.phase[0],'angle':component_step,'dt':dt}
                if n.startswith('ik_'):self.case['max_ik_component_rotation_step_deg']=max(self.case['max_ik_component_rotation_step_deg'],component_step)
                if step>self.case['max_local_rotation_step_deg']:
                    self.case['max_local_rotation_step_deg']=step
                    self.case['max_rotation_step_detail']={'bone':n,'phase':self.phase[0],'phase_elapsed':snapshot['time']-self.phase_started,'angle':step,'dt':dt}
                self.case['max_rotation_speed_deg_s']=max(self.case['max_rotation_speed_deg_s'],step/dt)
                if not n.startswith('ik_'):
                    self.case['max_deform_rotation_step_deg']=max(self.case['max_deform_rotation_step_deg'],step)
                if step>75 and not n.startswith('ik_') and len(self.case['outliers'])<40:
                    self.case['outliers'].append({'phase':self.phase[0],'bone':n,'angle':step,'dt':dt,'time':snapshot['time']})
        self.previous=snapshot
        elapsed=snapshot['time']-self.phase_started
        if self.phase[0]=='run' and elapsed>1:
            assert phase['max_speed']>100,'Fixture did not drive real character movement'
            assert any(n['valid'] and n['weight']>.01 for n in snapshot['motion_matching']),'Fixture has no valid active MM result'
        if FLOOR_Z>0 and self.phase[0]=='sprint' and elapsed>2:
            assert phase['max_speed']>600,'Dry fixture did not enter actual Sprint'
        if not self.phase_snapshot and elapsed>self.phase[1]*.65:
            RESULT['phase_snapshots'].append({'case':self.case['name'],'phase':self.phase[0],'snapshot':snapshot});self.phase_snapshot=True
            if self.phase[0]=='sprint':(OUT/'sprint_probe.json').write_text(json.dumps(snapshot,indent=2),encoding='utf8')
    def tick(self,dt):
        try:
            if time.monotonic()-self.started>480:raise RuntimeError('PIE comparison deadline exceeded')
            if self.state=='ending':
                if time.monotonic()<self.end_at or LEVEL.is_in_play_in_editor():return
                self.mode.set_editor_property('player_controller_class',self.original_controller);self.mode.set_editor_property('default_pawn_class',self.original_pawn)
                self.performance.set_editor_property('bThrottleCPUWhenNotForeground',self.original_throttle);execute('t.MaxFPS '+str(self.original_fps))
                unreal.SWAudioAuthoringLibrary.restore_audio_pie(self.original_pie)
                unreal.unregister_slate_post_tick_callback(self.handle);unreal.EditorPythonScripting.set_keep_python_script_alive(False);unreal.SystemLibrary.quit_editor();return
            if self.state=='loading':
                self.world=EDITOR.get_game_world()
                if not self.world:return
                self.pc=unreal.GameplayStatics.get_player_controller(self.world,0)
                if not self.pc:return
                RESULT['controller_class']=self.pc.get_class().get_path_name()
                assert isinstance(self.pc,unreal.BasePlayerController),'Fixture must use production controller'
                RESULT['world']=unreal.SWAudioAuthoringLibrary.describe_audio_world(self.world)
                assert 'NetMode=0' in RESULT['world'],'Use standalone PIE, not a dedicated server pose'
                if self.pc.get_controlled_pawn():self.pc.get_controlled_pawn().destroy_actor()
                self.state='testing';self.next_case();return
            if self.state!='testing':return
            name,duration,right,forward,sprint,jump,yaw=self.phase
            if right or forward:self.pawn.do_move(right,forward)
            else:self.pawn.stop_move_input()
            snapshot=json.loads(LIB.snapshot_character(self.pawn));self.record(snapshot)
            if snapshot['time']-self.phase_started>=duration:self.next_phase()
        except Exception:self.fail()

RUNNER=Runner()
