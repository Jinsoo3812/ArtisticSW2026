import json
worlds=unreal.SWAudioAuthoringLibrary.get_audio_pie_worlds()
server=next(w for w in worlds if 'NetMode=2 ' in unreal.SWAudioAuthoringLibrary.describe_audio_world(w))
worlds=[server,next(w for w in worlds if w!=server)]
pcs=[unreal.GameplayStatics.get_player_controller(w,0) for w in worlds]
assert all(pc and pc.get_controlled_pawn() for pc in pcs),'Wait for both PIE controllers'
owners=[pc.get_controlled_pawn() for pc in pcs]
zooms=[p.get_component_by_class(unreal.SWPlayerCameraZoomComponent) for p in owners]
assert all(zooms)
sub=unreal.get_default_object(unreal.load_class(None,'/Script/Engine.SubsystemBlueprintLibrary'))
inputs=[sub.call_method('GetLocalPlayerSubSystemFromPlayerController',args=(pc,unreal.EnhancedInputLocalPlayerSubsystem.static_class())) for pc in pcs]
actions=[next(a for a in unreal.ObjectIterator(unreal.InputAction) if a.get_outer()==z) for z in zooms]
assert all(actions),'Native zoom action/context was not bound'
ships=[next(s for s in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.Ship) if not isinstance(s,unreal.EnemyShip) and not s.get_editor_property('hidden')) for w in worlds]
for w,ship in zip(worlds,ships):
    for p in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.BasePlayer):
        i=[a.get_name() for a in owners].index(p.get_name())
        p.set_actor_location(unreal.MathLibrary.transform_location(ship.get_actor_transform(),unreal.Vector(-300+200*i,0,440)),False,True)
        p.get_editor_property('character_movement').set_movement_mode(unreal.MovementMode.MOVE_FALLING,0)
for w in worlds:
    for ai in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.AIController):
        ai.stop_movement();brain=ai.get_editor_property('brain_component')
        if brain:brain.stop_logic('Manual zoom input validation')
for pc,p,z in zip(pcs,owners,zooms):
    pc.set_view_target_with_blend(p,0.);pc.set_editor_property('show_mouse_cursor',False);z.set_preferred_distance(400.)
phases=[('host_zoom_in',0,1.),('client_zoom_out',1,-1.),('minimum',0,1.),('maximum',0,-1.),('ui_blocks',0,1.),('selection_blocks',0,1.),('restore',0,0.),('water_zoom_in',1,1.),('water_zoom_out',0,-1.),('final',0,0.)]
began=None;phase=-1;phase_began=0.;pulse=False;last_sample=-1.;rows=[];done=False
def on_tick(delta):
    global began,phase,phase_began,pulse,last_sample,done
    if done:return
    now=unreal.GameplayStatics.get_time_seconds(server)
    if began is None:began=now
    new_phase=int((now-began)/2.5)
    if new_phase>=len(phases):
        for pc,z,inp,action in zip(pcs,zooms,inputs,actions):
            pc.set_editor_property('show_mouse_cursor',False);z.reserve_wheel(pc,False);inp.inject_input_vector_for_action(action,unreal.Vector(),[],[])
        (OUT/'manual_zoom_samples.json').write_text(json.dumps(rows,indent=2))
        (OUT/'manual_zoom_complete.json').write_text(json.dumps({'owners':[p.get_name() for p in owners],'phases':phases,'samples':len(rows)},indent=2))
        done=True;return
    if new_phase!=phase:
        phase=new_phase;phase_began=now;pulse=False
        label,active,axis=phases[phase]
        for pc,z in zip(pcs,zooms):pc.set_editor_property('show_mouse_cursor',False);z.reserve_wheel(pc,False)
        if label=='ui_blocks':pcs[0].set_editor_property('show_mouse_cursor',True)
        if label=='selection_blocks':zooms[0].reserve_wheel(pcs[0],True)
        if label=='restore':
            for z in zooms:z.set_preferred_distance(400.)
        if label=='water_zoom_in':
            for w,ship in zip(worlds,ships):
                for p in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.BasePlayer):
                    i=[a.get_name() for a in owners].index(p.get_name())
                    p.set_actor_location(ship.get_actor_location()+unreal.Vector(-2500,5500+700*i,100),False,True)
                    p.get_editor_property('character_movement').set_movement_mode(unreal.MovementMode.MOVE_FALLING,0)
    label,active,axis=phases[phase]
    elapsed=now-phase_began
    send=elapsed>.6 and (not pulse or label in ['minimum','maximum']) and label!='final'
    for i,(inp,action) in enumerate(zip(inputs,actions)):
        inp.inject_input_vector_for_action(action,unreal.Vector(axis if send and i==active else 0,0,0),[],[])
    if send:pulse=True
    if now-last_sample<.1:return
    last_sample=now
    sample={'phase':label,'elapsed':elapsed,'worlds':[]}
    for w,p,z in zip(worlds,owners,zooms):
        arm=p.get_editor_property('camera_boom');cam=p.get_editor_property('follow_camera')
        sample['worlds'].append({'world':unreal.SWAudioAuthoringLibrary.describe_audio_world(w),'owner':p.get_name(),'preferred':z.preferred_distance,'target':arm.target_arm_length,'distance':p.get_actor_location().distance(cam.get_world_location()),'collision':arm.get_editor_property('bDoCollisionTest'),'swimming':p.get_component_by_class(unreal.SwimmingComponent).is_custom_swimming(),'can_zoom':p.can_adjust_manual_camera_zoom() if hasattr(p,'can_adjust_manual_camera_zoom') else None})
    rows.append(sample)
result={'owners':[p.get_name() for p in owners],'phases':phases,'actions':[a.get_path_name() for a in actions]}
