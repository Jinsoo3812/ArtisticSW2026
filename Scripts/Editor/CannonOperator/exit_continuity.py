import json
exec((OUT.parent.parent/'LiveScripts/continuity.py').read_text(encoding='utf-8-sig'),globals())
f=live_api['cannon_test'];worlds=f['worlds'];pcs=f['pcs'];names=f['names'];server=worlds[0]
sub=unreal.get_default_object(unreal.load_class(None,'/Script/Engine.SubsystemBlueprintLibrary'))
inputs=[sub.call_method('GetLocalPlayerSubSystemFromPlayerController',args=(pc,unreal.EnhancedInputLocalPlayerSubsystem.static_class())) for pc in pcs]
can=[pc.get_controlled_pawn() for pc in pcs];players=f['owners']
continuity=CannonContinuityProbe(worlds,names,diagnostics=True)
began=None;sent=False;done=False

def on_tick(delta):
    global began,sent,done
    if done:return
    now=unreal.GameplayStatics.get_time_seconds(server)
    if began is None:began=now
    elapsed=now-began
    if elapsed>=2 and not sent:
        for inp,c in zip(inputs,can):inp.inject_input_vector_for_action(c.get_editor_property('cannon_exit_action'),unreal.Vector(1,0,0),[],[])
        sent=True
    stage='mounted' if elapsed<2 else ('release' if elapsed<4 else 'idle')
    continuity.sample(now,stage)
    if elapsed>6:
        continuity.save(OUT/'cannon_exit_continuity.json');done=True
result={'requested':'per-frame mounted to exit and idle pose continuity','duration':6}
