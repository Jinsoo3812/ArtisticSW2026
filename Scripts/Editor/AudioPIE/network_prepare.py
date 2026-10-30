import json
worlds=unreal.SWAudioAuthoringLibrary.get_audio_pie_worlds()
server=next(w for w in worlds if 'NetMode=2 ' in unreal.SWAudioAuthoringLibrary.describe_audio_world(w))
assert len(worlds)==2
lib=unreal.get_default_object(unreal.GameplayStatics)
def allof(w,c): return unreal.GameplayStatics.get_all_actors_of_class(w,c)
def spawn(w,cls,pos):
 t=unreal.Transform(location=pos)
 a=lib.call_method('BeginDeferredActorSpawnFromClass',args=(w,cls,t,unreal.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN,None,unreal.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
 lib.call_method('FinishSpawningActor',args=(a,t,unreal.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT));return a
player_ship=next(s for s in allof(server,unreal.Ship) if not isinstance(s,unreal.EnemyShip) and not s.get_editor_property('hidden'))
origin=player_ship.get_actor_location()
for i,pc in enumerate(allof(server,unreal.PlayerController)):
 pawn=pc.get_controlled_pawn()
 if pawn:
  pawn.set_actor_location(unreal.Vector(origin.x-1600-i*400,origin.y,origin.z+700),False,True)
  if isinstance(pawn,unreal.Character):pawn.get_editor_property('character_movement').disable_movement()
enemy=spawn(server,unreal.load_class(None,'/Game/GameplayAbilitySystem/Enemy/Balancing/Final/N1/BP_ES_Normal_MRR.BP_ES_Normal_MRR_C'),unreal.Vector(origin.x,origin.y+3500,origin.z))
enemy.set_editor_property('bEnableDistanceOptimization',False)
enemy.get_navigation_component().set_navigation_enabled(False)
ai=enemy.get_controller()
if isinstance(ai,unreal.AIController):
 ai.stop_movement()
 brain=ai.get_editor_property('brain_component')
 if brain:brain.stop_logic('Controlled audio validation')
pattern=enemy.get_pattern_runtime_component()
if pattern:pattern.set_component_tick_enabled(False)
cameras=[]
for w in worlds:
 for a in allof(w,unreal.Actor):
  if isinstance(a,unreal.SWOceanAudioDirector):a.set_editor_property('bEnabled',False)
  if isinstance(a,unreal.SWNetworkWeatherActor):a.set_actor_tick_enabled(False)
  if isinstance(a,unreal.BaseEnemy):
   ai=a.get_controller()
   if isinstance(ai,unreal.AIController):
    ai.stop_movement()
    brain=ai.get_editor_property('brain_component')
    if brain:brain.stop_logic('Controlled audio validation')
   a.get_editor_property('character_movement').disable_movement()
  f=a.get_component_by_class(unreal.SWFoleyComponent)
  if f:f.set_editor_property('bEnabled',False)
  if 'WeatherEvent_Effect' in a.get_class().get_name():
   a.set_actor_tick_enabled(False)
   for t in a.get_components_by_class(unreal.TimelineComponent):t.stop()
  for ac in a.get_components_by_class(unreal.AudioComponent):ac.stop()
 pc=unreal.GameplayStatics.get_player_controller(w,0)
 cam=spawn(w,unreal.CameraActor.static_class(),unreal.Vector(origin.x-1200,origin.y+1000,origin.z+1200))
 pc.set_view_target_with_blend(cam,0.)
 pc.set_audio_listener_attenuation_override(cam.get_editor_property('root_component'),unreal.Vector())
 cameras.append(cam)
live_api['network_fixture']={'worlds':worlds,'server':server,'ships':[player_ship,enemy],'origin':origin,'spawn':spawn,'allof':allof,'cameras':cameras}
result={'worlds':[unreal.SWAudioAuthoringLibrary.describe_audio_world(w) for w in worlds],'ships':[s.get_path_name() for s in [player_ship,enemy]],'origin':str(origin)}
(OUT/'network_fixture.json').write_text(json.dumps(result,indent=2))
