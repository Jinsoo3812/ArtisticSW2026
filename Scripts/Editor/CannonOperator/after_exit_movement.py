import json
f=live_api['cannon_test'];worlds=f['worlds'];pcs=f['pcs'];server=worlds[0]
sub=unreal.get_default_object(unreal.load_class(None,'/Script/Engine.SubsystemBlueprintLibrary'))
inputs=[sub.call_method('GetLocalPlayerSubSystemFromPlayerController',args=(pc,unreal.EnhancedInputLocalPlayerSubsystem.static_class())) for pc in pcs]
players=[pc.get_controlled_pawn() for pc in pcs];actions=[p.get_editor_property('move_action') for p in players]
starts=[p.get_actor_location() for p in players]
server_players=[next(p2 for p2 in unreal.GameplayStatics.get_all_actors_of_class(server,unreal.BasePlayer) if p2.get_name()==p.get_name()) for p in players]
server_starts=[p.get_actor_location() for p in server_players]
max_horizontal=[0.,0.];max_server_horizontal=[0.,0.]
c=next(c for c in unreal.GameplayStatics.get_all_actors_of_class(server,unreal.Cannon) if c.get_name()==f['cannon_names'][0])
ship=c.get_attach_parent_actor()
while ship and not isinstance(ship,unreal.Ship):ship=ship.get_attach_parent_actor()
ships=[next(s for s in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.Ship) if s.get_name()==ship.get_name()) for w in worlds]
began=None;done=False

def on_tick(delta):
    global began,done
    if done:return
    now=unreal.GameplayStatics.get_time_seconds(server)
    if began is None:began=now
    elapsed=now-began
    for i,(p,sp) in enumerate(zip(players,server_players)):
        for actor,peaks in [(p,max_horizontal),(sp,max_server_horizontal)]:
            velocity=actor.get_velocity();peaks[i]=max(peaks[i],(velocity.x**2+velocity.y**2)**.5)
    # Walk backward into the deck, away from the cannon's physical obstruction.
    for inp,act in zip(inputs,actions):inp.inject_input_vector_for_action(act,unreal.Vector(0,-1 if elapsed<.5 else 0,0),[],[])
    if elapsed>3:
        rows=[]
        for i,p in enumerate(players):
            sp=server_players[i]
            relative=unreal.MathLibrary.inverse_transform_location(ships[i].get_actor_transform(),p.get_actor_location())
            server_relative=unreal.MathLibrary.inverse_transform_location(ships[0].get_actor_transform(),sp.get_actor_location())
            rows.append({'name':p.get_name(),'moved_cm':p.get_actor_location().distance(starts[i]),'server_moved_cm':sp.get_actor_location().distance(server_starts[i]),'peak_horizontal_speed':max_horizontal[i],'server_peak_horizontal_speed':max_server_horizontal[i],'server_error_cm':sp.get_actor_location().distance(p.get_actor_location()),'ship_relative_error_cm':relative.distance(server_relative),'movement':str(p.get_editor_property('character_movement').movement_mode),'velocity':str(p.get_velocity())})
        (OUT/'cannon_after_exit_movement.json').write_text(json.dumps(rows,indent=2));done=True
result={'requested':'actual move input after normal exit and cannon destruction'}
