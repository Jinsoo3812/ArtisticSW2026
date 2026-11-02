import json, math
worlds=unreal.SWAudioAuthoringLibrary.get_audio_pie_worlds()
assert len(worlds)==2
server=next(w for w in worlds if 'NetMode=2 ' in unreal.SWAudioAuthoringLibrary.describe_audio_world(w))
worlds=[server,next(w for w in worlds if w!=server)]
pcs=[unreal.GameplayStatics.get_player_controller(w,0) for w in worlds]
owners=[pc.get_controlled_pawn() for pc in pcs]
assert all(isinstance(p,unreal.BasePlayer) for p in owners)
names=[p.get_name() for p in owners]
ships=[next(s for s in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.Ship) if not isinstance(s,unreal.EnemyShip) and not s.get_editor_property('hidden')) for w in worlds]
for w in worlds:
    for ai in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.AIController):
        ai.stop_movement()
        brain=ai.get_editor_property('brain_component')
        if brain:brain.stop_logic('Bounded camera validation')
for pc,p in zip(pcs,owners):pc.set_view_target_with_blend(p,0.)
phases=[('pillars_native',False,False),('pillars_policy',True,False),('pillars_native_repeat',False,False),('pillars_policy_repeat',True,False),('other_player_native',False,True),('other_player_policy',True,True),('wall_native',False,False),('wall_policy',True,False),('enemy_native',False,False),('enemy_policy',True,False)]
lib=unreal.get_default_object(unreal.GameplayStatics)
enemy_names=[]
for i in range(2):
    tf=unreal.Transform(location=ships[0].get_actor_location()+unreal.Vector(0,0,2000+i*500))
    cls=unreal.load_class(None,'/Game/GameplayAbilitySystem/Enemy/BP_DeckMeleeEnemy.BP_DeckMeleeEnemy_C')
    e=lib.call_method('BeginDeferredActorSpawnFromClass',args=(server,cls,tf,unreal.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN,None,unreal.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
    lib.call_method('FinishSpawningActor',args=(e,tf,unreal.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
    enemy_names.append(e.get_name())
began=None; rows=[];last_sample=-1.;done=False
def vec(v):return [v.x,v.y,v.z]
def on_tick(delta):
    global began,last_sample,done
    if done:return
    now=unreal.GameplayStatics.get_time_seconds(server)
    if began is None:began=now
    phase=int((now-began)/3.)
    if phase>=len(phases):
        for p in owners:p.get_editor_property('camera_boom').set_editor_property('bEnableObstaclePolicy',True)
        (OUT/'camera_network_samples.json').write_text(json.dumps(rows,indent=2))
        (OUT/'camera_network_complete.json').write_text(json.dumps({'owners':names,'samples':len(rows),'phases':phases,'enemy_fixtures':enemy_names},indent=2))
        done=True;return
    label,enabled,crowd=phases[phase]
    elapsed=(now-began)%3.
    for wi,(w,ship,pc) in enumerate(zip(worlds,ships,pcs)):
        tf=ship.get_actor_transform()
        # Camera-only fixture: park both replicated characters on the real ship.
        for p in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.BasePlayer):
            if p.get_name() not in names:continue
            index=names.index(p.get_name())
            local=unreal.Vector(-300+200*index,120,500)
            if crowd:local=unreal.Vector(150+150*index,0,450)
            if label.startswith('wall'):local=unreal.Vector(100+200*index,300,200)
            p.get_editor_property('character_movement').disable_movement()
            p.set_actor_location(unreal.MathLibrary.transform_location(tf,local),False,True)
            p.get_editor_property('camera_boom').set_editor_property('bEnableObstaclePolicy',enabled)
        for e in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.BaseEnemy):
            if e.get_name() not in enemy_names:continue
            ei=enemy_names.index(e.get_name())
            ai=e.get_controller()
            if isinstance(ai,unreal.AIController):
                ai.stop_movement()
                brain=ai.get_editor_property('brain_component')
                if brain:brain.stop_logic('Camera enemy fixture')
            e.get_editor_property('character_movement').disable_movement()
            pos=unreal.Vector(-300+200*ei,260,500) if label.startswith('enemy') else unreal.Vector(-2500,0,1200)
            e.set_actor_location(unreal.MathLibrary.transform_location(tf,pos),False,True)
        pc.set_control_rotation(unreal.Rotator(pitch=0,yaw=ship.get_actor_rotation().yaw+(-90 if not crowd else (180 if wi==0 else 0)),roll=0))
    if elapsed<1. or now-last_sample<.1:return
    last_sample=now
    sample={'phase':label,'time':now,'worlds':[]}
    for w,p,ship in zip(worlds,owners,ships):
        arm=p.get_editor_property('camera_boom');cam=p.get_editor_property('follow_camera')
        blocker=arm.get_editor_property('last_blocking_component')
        sample['worlds'].append({'world':unreal.SWAudioAuthoringLibrary.describe_audio_world(w),'owner':p.get_name(),'local':p.is_locally_controlled(),'location':vec(p.get_actor_location()),'camera':vec(cam.get_world_location()),'distance':p.get_actor_location().distance(cam.get_world_location()),'fixed':arm.is_collision_fix_applied(),'ignored':arm.get_editor_property('last_ignored_component_count'),'budget':arm.get_editor_property('bLastSweepBudgetExhausted'),'blocker':blocker.get_name() if blocker else None,'ship_rotation':str(ship.get_actor_rotation()),'swimming':p.get_component_by_class(unreal.SwimmingComponent).is_custom_swimming()})
    rows.append(sample)
result={'phases':phases,'owners':names}
