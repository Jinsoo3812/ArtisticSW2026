import json,math
worlds=unreal.SWAudioAuthoringLibrary.get_audio_pie_worlds()
assert len(worlds)==2
server=next(w for w in worlds if 'NetMode=2 ' in unreal.SWAudioAuthoringLibrary.describe_audio_world(w))
worlds=[server,next(w for w in worlds if w!=server)]
pcs=[unreal.GameplayStatics.get_player_controller(w,0) for w in worlds]
owners=[pc.get_controlled_pawn() for pc in pcs];names=[p.get_name() for p in owners]
ships=[next(s for s in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.Ship) if not isinstance(s,unreal.EnemyShip) and not s.get_editor_property('hidden')) for w in worlds]
sub=unreal.get_default_object(unreal.load_class(None,'/Script/Engine.SubsystemBlueprintLibrary'))
inputs=[sub.call_method('GetLocalPlayerSubSystemFromPlayerController',args=(pc,unreal.EnhancedInputLocalPlayerSubsystem.static_class())) for pc in pcs]
actions=[p.get_editor_property('move_action') for p in owners]
base=[('cabin_walk',6),('fall_to_water',7),('surface_forward',5),('dive',4),('underwater_forward',5),('ascend',6),('return_to_cabin',7)]
phases=[(str(cycle)+'_'+name,seconds) for cycle in [1] for name,seconds in base]
phase=-1;began=0.;last_sample=-1.;rows=[];done=False
def vec(v):return [v.x,v.y,v.z]
def on_tick(delta):
    global phase,began,last_sample,done
    if done:return
    now=unreal.GameplayStatics.get_time_seconds(server)
    if phase<0 or now-began>=phases[phase][1]:
        phase+=1
        if phase>=len(phases):
            for inp,action,p in zip(inputs,actions,owners):
                inp.inject_input_vector_for_action(action,unreal.Vector(),[],[])
                p.stop_swim_dive();p.do_jump_end()
            (OUT/'cannon_after_exit_swim_samples.json').write_text(json.dumps(rows,indent=2))
            (OUT/'cannon_after_exit_swim_complete.json').write_text(json.dumps({'phases':phases,'samples':len(rows),'owners':names},indent=2))
            done=True;return
        began=now;label=phases[phase][0];unreal.log('SW_CAMERA_SWIM_PHASE '+label)
        for p in owners:p.stop_swim_dive();p.do_jump_end()
        if label.endswith(('cabin_walk','fall_to_water','return_to_cabin')):
            for w,ship in zip(worlds,ships):
                for p in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.BasePlayer):
                    if p.get_name() not in names:continue
                    index=names.index(p.get_name())
                    if label.endswith('fall_to_water'):
                        pos=ship.get_actor_location()+unreal.Vector(-2500,5500+index*700,1000)
                    else:
                        pos=unreal.MathLibrary.transform_location(ship.get_actor_transform(),unreal.Vector(-300+200*index,0,440))
                    mov=p.get_editor_property('character_movement');mov.stop_movement_immediately()
                    p.set_actor_location(pos,False,True)
                    mov.set_movement_mode(unreal.MovementMode.MOVE_FALLING,0)
            for pc,ship in zip(pcs,ships):pc.set_control_rotation(unreal.Rotator(pitch=0,yaw=ship.get_actor_rotation().yaw,roll=0))
        if label.endswith('dive'):
            for p in owners:p.start_swim_dive()
        if label.endswith('ascend'):
            for p in owners:p.do_jump_start()
    label=phases[phase][0]
    moving=label.endswith(('cabin_walk','surface_forward','underwater_forward'))
    for inp,action in zip(inputs,actions):inp.inject_input_vector_for_action(action,unreal.Vector(0,1,0) if moving else unreal.Vector(),[],[])
    if now-last_sample<.1:return
    last_sample=now
    sample={'phase':label,'time':now,'elapsed':now-began,'worlds':[]}
    for w,p in zip(worlds,owners):
        arm=p.get_editor_property('camera_boom');swim=p.get_component_by_class(unreal.SwimmingComponent);cam=p.get_editor_property('follow_camera');mov=p.get_editor_property('character_movement')
        sample['worlds'].append({'world':unreal.SWAudioAuthoringLibrary.describe_audio_world(w),'owner':p.get_name(),'local':p.is_locally_controlled(),'location':vec(p.get_actor_location()),'velocity':vec(p.get_velocity()),'swimming':swim.is_custom_swimming(),'underwater':swim.is_underwater(),'movement_mode':str(mov.movement_mode),'custom_mode':mov.custom_movement_mode,'camera_lag':arm.get_editor_property('bEnableCameraLag'),'camera_distance':p.get_actor_location().distance(cam.get_world_location()),'budget':arm.get_editor_property('bLastSweepBudgetExhausted'),'policy':arm.get_editor_property('bEnableObstaclePolicy')})
    # Include both authoritative copies to distinguish local camera state from replication.
    sample['server_players']=[{'name':p.get_name(),'location':vec(p.get_actor_location()),'swimming':p.get_component_by_class(unreal.SwimmingComponent).is_custom_swimming(),'underwater':p.get_component_by_class(unreal.SwimmingComponent).is_underwater()} for p in unreal.GameplayStatics.get_all_actors_of_class(server,unreal.BasePlayer) if p.get_name() in names]
    rows.append(sample)
result={'phases':phases,'owners':names}
