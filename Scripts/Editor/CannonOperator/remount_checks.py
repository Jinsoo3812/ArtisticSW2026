import json
f=live_api['cannon_test'];worlds=f['worlds'];pcs=f['pcs'];server=worlds[0];can=[pc.get_controlled_pawn() for pc in pcs]
assert all(isinstance(c,unreal.Cannon) for c in can)
sub=unreal.get_default_object(unreal.load_class(None,'/Script/Engine.SubsystemBlueprintLibrary'))
inputs=[sub.call_method('GetLocalPlayerSubSystemFromPlayerController',args=(pc,unreal.EnhancedInputLocalPlayerSubsystem.static_class())) for pc in pcs]
start=[c.get_editor_property('base_mesh').get_editor_property('relative_rotation').yaw for c in can]
actions=[c.get_editor_property('cannon_look_action') for c in can]
contexts=[c.get_editor_property('cannon_input_mapping_context') for c in can]
began=None;sent=False;released=False;done=False;result_rows={}
def on_tick(delta):
    global began,sent,released,done
    if done:return
    now=unreal.GameplayStatics.get_time_seconds(server)
    if began is None:began=now
    elapsed=now-began
    if not released:
        pulse=elapsed>.5 and not sent
        for inp,act in zip(inputs,actions):inp.inject_input_vector_for_action(act,unreal.Vector(1 if pulse else 0,0,0),[],[])
        if pulse:sent=True
    if elapsed>2 and not released:
        result_rows['remount']=[{'cannon':c.get_name(),'yaw_delta':c.get_editor_property('base_mesh').get_editor_property('relative_rotation').yaw-start[i],'input_components':len([o for o in unreal.ObjectIterator(unreal.EnhancedInputComponent) if o.get_outer()==c])} for i,c in enumerate(can)]
        inputs[0].inject_input_vector_for_action(can[0].get_editor_property('cannon_exit_action'),unreal.Vector(1,0,0),[],[])
        remote_server=next(c for c in unreal.GameplayStatics.get_all_actors_of_class(server,unreal.Cannon) if c.get_name()==can[1].get_name())
        assert unreal.SWCannonOperatorAuthoringLibrary.queue_operator_destroy(remote_server)
        released=True
    if elapsed>5:
        result_rows['after_exit_and_destroy']=[]
        for pc,context in zip(pcs,contexts):
            p=pc.get_controlled_pawn()
            inp=sub.call_method('GetLocalPlayerSubSystemFromPlayerController',args=(pc,unreal.EnhancedInputLocalPlayerSubsystem.static_class()))
            # UFUNCTION bool + out int becomes optional priority in Python: None on false.
            result_rows['after_exit_and_destroy'].append({'pawn':p.get_name() if p else None,'is_character':isinstance(p,unreal.BasePlayer),'attached':p.get_attach_parent_actor().get_name() if p and p.get_attach_parent_actor() else None,'walking':p.get_editor_property('character_movement').movement_mode==unreal.MovementMode.MOVE_WALKING if isinstance(p,unreal.BasePlayer) else False,'cannon_context_remains':inp.has_mapping_context(context) is not None})
        (OUT/'cannon_remount_checks.json').write_text(json.dumps(result_rows,indent=2));done=True
result={'requested':'remount, one input pulse, normal host exit, client cannon destruction'}
