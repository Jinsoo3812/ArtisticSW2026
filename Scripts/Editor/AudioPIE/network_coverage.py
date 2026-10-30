"""Transient two-world checks. Run after network_prepare; never save a PIE map."""
import json
from pathlib import Path
OUT=OUT/'CoverageVerified';OUT.mkdir(exist_ok=True)
f=live_api['network_fixture'];worlds=f['worlds'];server=f['server'];ships=f['ships']
allof=f['allof'];spawn=f['spawn'];lib=unreal.get_default_object(unreal.load_class(None,'/Script/Engine.SubsystemBlueprintLibrary'))
directors=[allof(w,unreal.SWOceanAudioDirector)[0] for w in worlds]
weathers=[allof(w,unreal.SWNetworkWeatherActor)[0] for w in worlds]
host=unreal.GameplayStatics.get_player_controller(server,0).get_controlled_pawn();host_name=host.get_name()
input_system=lib.call_method('GetLocalPlayerSubSystemFromPlayerController',args=(unreal.GameplayStatics.get_player_controller(server,0),unreal.EnhancedInputLocalPlayerSubsystem.static_class()))
move=host.get_editor_property('move_action')
points=[s.get_components_by_class(unreal.ShipRepairPointComponent) for s in ships]
assert all(len(p)>=3 for p in points)
origin=ships[0].get_actor_location();drop=unreal.Vector(origin.x-2500,origin.y+5500,1200)
weather_enum=type(weathers[0].get_editor_property('InitWeather'))
phases=[('npc_entry',12),('player_float',12),('player_swim',7),('leak_player',5),('leak_enemy',5),('leak_many',5),('leak_off',4),('leak_far',4),('leak_return',5),('leak_hidden',4),('leak_resume',4),('leak_final_off',3),('rain_out',6),('rain_cabin',10),('rain_out_again',6),('birds_day',25),('birds_night',12)]
phase=-1;began=0.;closing=0.;stopped_second=False;done=False;last_sample=0.;rows=[];npc_name=None;mute_mix=None
cabin_found=None;coast_index=0;last_coast_try=0.
shore=list(directors[0].get_editor_property('shore_points'))
shore.sort(key=lambda p:(p.x-origin.x)**2+(p.y-origin.y)**2)

def set_leaks(player,enemy):
 for ship,count in zip(ships,[player,enemy]):assert unreal.SWAudioAuthoringLibrary.queue_audio_probe_leaks(ship,count)

def place_cameras(pos):
 for cam in f['cameras']:cam.set_actor_location(pos,False,True)

def weather(hour,kind):
 for a in weathers:a.set_actor_tick_enabled(True)
 weathers[0].set_network_weather_time(hour,0,kind.value)
 for w in worlds:
  for a in allof(w,unreal.Actor):
   if 'WeatherEvent_Effect' not in a.get_class().get_name():continue
   a.set_actor_tick_enabled(False)
   for t in a.get_components_by_class(unreal.TimelineComponent):t.stop()
   for c in a.get_components_by_class(unreal.AudioComponent):c.stop()
   a.call_method('WeatherEvent',args=(kind,))

def enter(label):
 global npc_name,mute_mix
 input_system.inject_input_vector_for_action(move,unreal.Vector(),[],[])
 if label=='npc_entry':
  cls=unreal.load_class(None,'/Game/GameplayAbilitySystem/Enemy/Balancing/T1/T1_BP_DeckMeleeEnemy.T1_BP_DeckMeleeEnemy_C')
  assert unreal.SWAudioAuthoringLibrary.queue_audio_probe_character(server,cls,drop+unreal.Vector(0,0,10000))
 if label=='player_float':
  for w in worlds:
   for p in allof(w,unreal.Character):
    if p.get_name()==host_name:
     p.set_actor_location(drop,False,True)
     if p.get_controller():p.get_editor_property('character_movement').set_movement_mode(unreal.MovementMode.MOVE_FALLING,0)
 if label.startswith('leak_'):
  for w in worlds:
   for p in allof(w,unreal.Character):
    p.get_editor_property('character_movement').disable_movement()
  ships[0].set_actor_hidden_in_game(label=='leak_hidden')
  set_leaks(*{'leak_player':(1,0),'leak_enemy':(0,1),'leak_many':(3,3),'leak_off':(0,0),'leak_far':(1,0),'leak_return':(1,0),'leak_hidden':(1,0),'leak_resume':(1,0),'leak_final_off':(0,0)}[label])
 if label=='rain_out':
  weather(12,weather_enum.RAINY)
  mute_mix=unreal.SoundMix()
  mute_mix.set_editor_property('sound_class_effects',[unreal.SoundClassAdjuster(sound_class_object=unreal.load_asset('/Game/Audio/World/Mix/SC_WorldSFX'),volume_adjuster=0.)])
  for w,d in zip(worlds,directors):
   d.set_editor_property('bEnabled',True);unreal.GameplayStatics.push_sound_mix_modifier(w,mute_mix)
 if label=='birds_day':
  for w in worlds:unreal.GameplayStatics.pop_sound_mix_modifier(w,mute_mix)
  weather(12,weather_enum.SUNNY)
 if label=='birds_night':weather(23,weather_enum.SUNNY)
 unreal.log('SW_AUDIO_COVERAGE_PHASE '+label)

def sample(label,now):
 result={'phase':label,'time':now,'elapsed':now-began,'worlds':[]}
 for w,d,weather_actor in zip(worlds,directors,weathers):
  service=lib.call_method('GetWorldSubsystem',args=(w,unreal.SWShipAudioSubsystem.static_class()))
  result['worlds'].append({'description':unreal.SWAudioAuthoringLibrary.describe_audio_world(w),'ship_audio':service.get_audio_state(),'cabin':d.get_editor_property('listener_in_cabin'),'underwater':d.get_editor_property('listener_underwater'),'ocean':d.get_editor_property('current_ocean_gain'),'shore_distance':d.get_editor_property('nearest_shore_distance'),'wildlife_allowed':weather_actor.allows_coastal_wildlife_audio(),'leaks':[{'ship':s.get_name(),'active':sum(1 for p in s.get_components_by_class(unreal.ShipRepairPointComponent) if p.is_leak_active())} for s in allof(w,unreal.Ship) if s.get_name() in [s.get_name() for s in ships]],'characters':[{'name':a.get_name(),'position':str(a.get_actor_location()),'velocity':str(a.get_velocity()),'swimming':a.get_component_by_class(unreal.SwimmingComponent).is_custom_swimming() if a.get_component_by_class(unreal.SwimmingComponent) else False} for a in allof(w,unreal.Character) if a.get_name() in [host_name,npc_name]]})
 return result

def stop(i):
 unreal.AudioMixerLibrary.stop_recording_output(worlds[i],unreal.AudioRecordingExportType.WAV_FILE,'network_verified_coverage_'+phases[phase][0]+'_device'+str(i),str(OUT),None,None)

def on_tick(delta):
 global phase,began,closing,stopped_second,done,last_sample,cabin_found,coast_index,last_coast_try,npc_name
 if done:return
 now=unreal.GameplayStatics.get_time_seconds(server);advance=phase<0
 if phase>=0 and not closing and now-began>=phases[phase][1]:
  stop(0);closing=now;stopped_second=False
  (OUT/'coverage_samples.json').write_text(json.dumps(rows,indent=2))
 if closing and not stopped_second and now-closing>=.6:stop(1);stopped_second=True
 if closing and now-closing>=1.2:advance=True;closing=0.
 if advance:
  phase+=1
  if phase>=len(phases):
   input_system.inject_input_vector_for_action(move,unreal.Vector(),[],[])
   done=True;(OUT/'coverage_complete.json').write_text(json.dumps({'npc':npc_name,'host':host_name,'cabin_found':str(cabin_found),'samples':len(rows)},indent=2));unreal.log('SW_AUDIO_COVERAGE_COMPLETE');return
  began=now;enter(phases[phase][0])
  for w in worlds:unreal.AudioMixerLibrary.start_recording_output(w,40,None)
 label=phases[phase][0]
 for ship in ships:
  root=ship.get_editor_property('BuoyancyRoot');root.set_physics_linear_velocity(unreal.Vector(),False);root.set_physics_angular_velocity_in_degrees(unreal.Vector(),False)
 for w in worlds:
  for a in allof(w,unreal.Character):
   npc_target=isinstance(a,unreal.BaseEnemy) and (a.get_actor_location().x-drop.x)**2+(a.get_actor_location().y-drop.y)**2<500**2 and a.get_actor_location().z>-10000
   if label=='npc_entry' and npc_target:
    if w==server:npc_name=a.get_name()
    ai=a.get_controller()
    if isinstance(ai,unreal.AIController):
     ai.stop_movement();brain=ai.get_editor_property('brain_component')
     if brain:brain.stop_logic('Replicated audio fall probe')
   fol=a.get_component_by_class(unreal.SWFoleyComponent)
   if fol:fol.set_editor_property('bEnabled',(label=='npc_entry' and npc_target) or (label in ['player_float','player_swim'] and a.get_name()==host_name))
 if label=='npc_entry':place_cameras(unreal.Vector(drop.x-300,drop.y-300,400))
 elif label in ['player_float','player_swim']:
  input_system.inject_input_vector_for_action(move,unreal.Vector(0,1 if label=='player_swim' else 0,0),[],[])
  p=host.get_actor_location();place_cameras(unreal.Vector(p.x-300,p.y-300,max(450,p.z+300)))
 elif label.startswith('leak_'):
  if label=='leak_many':
   a=ships[0].get_actor_location();b=ships[1].get_actor_location();pos=unreal.Vector((a.x+b.x)/2,(a.y+b.y)/2,350)
  else:
   pos=points[1 if label=='leak_enemy' else 0][0].get_world_location();pos=unreal.Vector(pos.x-200+(10000 if label=='leak_far' else 0),pos.y-200,pos.z+200)
  place_cameras(pos)
 elif label.startswith('rain_'):
  for w,cam in zip(worlds,f['cameras']):
   ship=next(s for s in allof(w,unreal.Ship) if s.get_name()==ships[0].get_name())
   pos=unreal.SWAudioAuthoringLibrary.find_audio_probe_cabin_point(ship) if label=='rain_cabin' else ship.get_actor_location()+unreal.Vector(0,-1000,1800)
   cam.set_actor_location(pos,False,True)
  if label=='rain_cabin' and all(d.get_editor_property('listener_in_cabin') for d in directors):cabin_found='occupied_voxel'
 elif label.startswith('birds_'):
  if label=='birds_day' and now-last_coast_try>1.2:
   if any(d.get_editor_property('current_ocean_gain')<.1 or d.get_editor_property('listener_underwater') for d in directors):coast_index=min(coast_index+1,len(shore)-1)
   last_coast_try=now
  p=shore[coast_index];place_cameras(unreal.Vector(p.x,p.y,700))
 for w,cam in zip(worlds,f['cameras']):
  for pc in allof(w,unreal.PlayerController):
   pawn=pc.get_controlled_pawn()
   if pawn and not (label in ['player_float','player_swim'] and pawn.get_name()==host_name):pawn.set_actor_location(cam.get_actor_location()+unreal.Vector(0,0,100),False,True)
 if now-last_sample>=.25:
  last_sample=now;rows.append(sample(label,now))
result={'phases':phases,'drop':str(drop),'points':[len(p) for p in points]}
