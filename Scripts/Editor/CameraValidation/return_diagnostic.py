import json
worlds=unreal.SWAudioAuthoringLibrary.get_audio_pie_worlds();server=next(w for w in worlds if 'NetMode=2 ' in unreal.SWAudioAuthoringLibrary.describe_audio_world(w));worlds=[server,next(w for w in worlds if w!=server)]
pcs=[unreal.GameplayStatics.get_player_controller(w,0) for w in worlds];owners=[pc.get_controlled_pawn() for pc in pcs];names=[p.get_name() for p in owners]
ships=[next(s for s in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.Ship) if not isinstance(s,unreal.EnemyShip) and not s.get_editor_property('hidden')) for w in worlds]
for w,ship in zip(worlds,ships):
    for p in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.BasePlayer):
        i=names.index(p.get_name());p.set_actor_location(unreal.MathLibrary.transform_location(ship.get_actor_transform(),unreal.Vector(-300+200*i,0,440)),False,True);p.get_editor_property('character_movement').set_movement_mode(unreal.MovementMode.MOVE_FALLING,0)
    for ai in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.AIController):
        ai.stop_movement();brain=ai.get_editor_property('brain_component')
        if brain:brain.stop_logic('Camera return diagnostic')
began=None;rows=[];last=-1.;done=False
def on_tick(delta):
    global began,last,done
    if done:return
    now=unreal.GameplayStatics.get_time_seconds(server)
    if began is None:began=now
    elapsed=now-began
    if elapsed>=12:
        for p in owners:p.get_editor_property('camera_boom').set_editor_property('bEnableObstaclePolicy',True)
        (OUT/'return_diagnostic.json').write_text(json.dumps(rows,indent=2));done=True;return
    enabled=elapsed>=6
    for pc,ship,p in zip(pcs,ships,owners):
        pc.set_control_rotation(unreal.Rotator(pitch=0,yaw=ship.get_actor_rotation().yaw,roll=0));p.get_editor_property('camera_boom').set_editor_property('bEnableObstaclePolicy',enabled)
    if now-last<.1:return
    last=now;sample={'elapsed':elapsed,'policy':enabled,'worlds':[]}
    for w,p,ship in zip(worlds,owners,ships):
        arm=p.get_editor_property('camera_boom');block=arm.get_editor_property('last_blocking_component');cam=p.get_editor_property('follow_camera');pos=unreal.MathLibrary.inverse_transform_location(ship.get_actor_transform(),p.get_actor_location())
        sample['worlds'].append({'world':unreal.SWAudioAuthoringLibrary.describe_audio_world(w),'distance':p.get_actor_location().distance(cam.get_world_location()),'blocker':block.get_name() if block else None,'relative_location':[pos.x,pos.y,pos.z],'swimming':p.get_component_by_class(unreal.SwimmingComponent).is_custom_swimming(),'grounded':p.get_editor_property('character_movement').is_moving_on_ground()})
    rows.append(sample)
result={'duration':12}
