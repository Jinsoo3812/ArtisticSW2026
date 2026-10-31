"""Lit scene captures with the actual deck and cannon, from three review angles."""
import json
f=live_api['cannon_test'];w=f['worlds'][0]
lib=unreal.get_default_object(unreal.GameplayStatics)
captures=[]
for p in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.BasePlayer):
    if p.get_name() not in f['names']:continue
    c=p.get_attach_parent_actor();tf=c.get_actor_transform()
    for view,offset in [('front',(-5,-260,120)),('side',(-170,-260,100)),('rear',(-300,-180,135)),('grip',(-100,-110,75)),('body_debug',(-170,-260,100)),('body_front',(50,-260,180))]:
        pos=unreal.MathLibrary.transform_location(tf,unreal.Vector(*offset))
        focus=unreal.MathLibrary.transform_location(tf,unreal.Vector(-60,0,23) if view=='grip' else unreal.Vector(-110,0,40))
        spawn=unreal.Transform(location=pos)
        a=lib.call_method('BeginDeferredActorSpawnFromClass',args=(w,unreal.SceneCapture2D.static_class(),spawn,unreal.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN,None,unreal.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
        lib.call_method('FinishSpawningActor',args=(a,spawn,unreal.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
        a.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(pos,focus),False)
        cap=a.get_component_by_class(unreal.SceneCaptureComponent2D)
        rt=unreal.RenderingLibrary.create_render_target2d(w,1024,1024,unreal.TextureRenderTargetFormat.RTF_RGBA8,unreal.LinearColor(0,0,0,1),False)
        cap.set_editor_property('texture_target',rt)
        cap.set_editor_property('capture_source',unreal.SceneCaptureSource.SCS_BASE_COLOR if view in ['grip','body_debug','body_front'] else unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR)
        cap.set_editor_property('fov_angle',60.)
        cap.set_editor_property('capture_every_frame',False)
        captures.append((a,cap,rt,p.get_name()+'_'+live_api.get('capture_stage','review')+'_'+view,c,offset,view))
began=None;done=False;capture_index=0;requested=False

def on_tick(delta):
    global began,done,capture_index,requested
    if done:return
    now=unreal.GameplayStatics.get_time_seconds(w)
    if began is None:began=now
    a,cap,rt,name,c,offset,view=captures[capture_index]
    tf=c.get_actor_transform();pos=unreal.MathLibrary.transform_location(tf,unreal.Vector(*offset));focus=unreal.MathLibrary.transform_location(tf,(unreal.Vector(-60,0,23) if view=='grip' else unreal.Vector(-100,0,30)))
    a.set_actor_location(pos,False,True);a.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(pos,focus),False)
    if not requested:
        cap.capture_scene();requested=True;began=now;return
    if now-began<.25:return
    unreal.RenderingLibrary.export_render_target(w,rt,str(OUT),name+'.png');a.destroy_actor()
    capture_index+=1;requested=False
    if capture_index==len(captures):done=True
result={'capture_names':[v[3] for v in captures]}
