import json
f=live_api['cannon_test'];worlds=f['worlds'];pcs=f['pcs'];names=f['names'];server=worlds[0]
sub=unreal.get_default_object(unreal.load_class(None,'/Script/Engine.SubsystemBlueprintLibrary'))
inputs=[sub.call_method('GetLocalPlayerSubSystemFromPlayerController',args=(pc,unreal.EnhancedInputLocalPlayerSubsystem.static_class())) for pc in pcs]
can=[pc.get_controlled_pawn() for pc in pcs]
for inp,c in zip(inputs,can):inp.inject_input_vector_for_action(c.get_editor_property('cannon_exit_action'),unreal.Vector(1,0,0),[],[])
began=None;done=False
def on_tick(delta):
    global began,done
    if done:return
    now=unreal.GameplayStatics.get_time_seconds(server)
    if began is None:began=now
    if now-began<2.:return
    rows=[]
    for w,pc in zip(worlds,pcs):
        p=pc.get_controlled_pawn()
        rows.append({'world':unreal.SWAudioAuthoringLibrary.describe_audio_world(w),'pawn':p.get_name() if p else None,'is_character':isinstance(p,unreal.BasePlayer),'attached':p.get_attach_parent_actor().get_name() if p and p.get_attach_parent_actor() else None,'movement':str(p.get_editor_property('character_movement').movement_mode) if isinstance(p,unreal.BasePlayer) else None,'cannon_hides':[c.is_operator_hidden_for_controller(pc) for c in can]})
    (OUT/'cannon_exit_checks.json').write_text(json.dumps(rows,indent=2))
    done=True
result={'exit_inputs_injected':True}
