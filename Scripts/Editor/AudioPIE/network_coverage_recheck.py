"""Use native world-timer spawning and a real occupied cabin voxel for rechecks."""
import json
OUT=OUT/'CoverageRecheckFinal';OUT.mkdir(exist_ok=True)
f=live_api['network_fixture'];worlds=f['worlds'];server=f['server'];ships=f['ships'];allof=f['allof']
dirs=[allof(w,unreal.SWOceanAudioDirector)[0] for w in worlds]
o=ships[0].get_actor_location();drop=unreal.Vector(o.x-2500,o.y+5500,1200)
phases=[('npc_entry',12),('rain_out',7),('rain_cabin',7),('rain_out_again',7)]
phase=-1;began=0.;closing=0.;second_stopped=False;done=False;rows=[];last_sample=0.;mute_mix=None;names={}
def enter(label):
 global mute_mix
 if label=='npc_entry':
  cls=unreal.load_class(None,'/Game/GameplayAbilitySystem/Enemy/Balancing/T1/T1_BP_DeckMeleeEnemy.T1_BP_DeckMeleeEnemy_C')
  # Keep client prediction above the sea during the initial replication warmup.
  assert unreal.SWAudioAuthoringLibrary.queue_audio_probe_character(server,cls,drop+unreal.Vector(0,0,10000))
 if label=='rain_out':
  # Isolate mix transitions from motion at the thin edge voxel used by this probe.
  for w in worlds:
   for ship in allof(w,unreal.Ship):ship.get_editor_property('BuoyancyRoot').set_simulate_physics(False)
  mute_mix=unreal.SoundMix()
  mute_mix.set_editor_property('sound_class_effects',[unreal.SoundClassAdjuster(sound_class_object=unreal.load_asset('/Game/Audio/World/Mix/SC_WorldSFX'),volume_adjuster=0.)])
  for w,d in zip(worlds,dirs):
   d.set_editor_property('bEnabled',True);unreal.GameplayStatics.push_sound_mix_modifier(w,mute_mix)
   weather=allof(w,unreal.SWNetworkWeatherActor)[0];weather.set_actor_tick_enabled(False)
   enum=type(weather.get_editor_property('InitWeather'))
   for a in allof(w,unreal.Actor):
    if 'WeatherEvent_Effect' not in a.get_class().get_name():continue
    a.set_actor_tick_enabled(False)
    for t in a.get_components_by_class(unreal.TimelineComponent):t.stop()
    for c in a.get_components_by_class(unreal.AudioComponent):c.stop()
    a.call_method('WeatherEvent',args=(enum.RAINY,))
 unreal.log('SW_AUDIO_COVERAGE_RECHECK_PHASE '+label)
def stop(i):unreal.AudioMixerLibrary.stop_recording_output(worlds[i],unreal.AudioRecordingExportType.WAV_FILE,'network_verified_recheck_'+phases[phase][0]+'_device'+str(i),str(OUT),None,None)
def on_tick(delta):
 global phase,began,closing,second_stopped,done,last_sample
 if done:return
 now=unreal.GameplayStatics.get_time_seconds(server);advance=phase<0
 if phase>=0 and not closing and now-began>=phases[phase][1]:stop(0);closing=now;second_stopped=False
 if closing and not second_stopped and now-closing>=.6:stop(1);second_stopped=True
 if closing and now-closing>=1.2:advance=True;closing=0.
 if advance:
  phase+=1
  if phase>=len(phases):
   for w in worlds:unreal.GameplayStatics.pop_sound_mix_modifier(w,mute_mix)
   (OUT/'samples.json').write_text(json.dumps(rows,indent=2));(OUT/'complete.json').write_text(json.dumps(names,indent=2));done=True;unreal.log('SW_AUDIO_COVERAGE_RECHECK_COMPLETE');return
  began=now;enter(phases[phase][0])
  for w in worlds:unreal.AudioMixerLibrary.start_recording_output(w,20,None)
 label=phases[phase][0]
 if label=='npc_entry':
  for ship in ships:
   root=ship.get_editor_property('BuoyancyRoot');root.set_physics_linear_velocity(unreal.Vector(),False);root.set_physics_angular_velocity_in_degrees(unreal.Vector(),False)
 frame={'phase':label,'time':now,'elapsed':now-began,'worlds':[]}
 for w,cam,d in zip(worlds,f['cameras'],dirs):
  ship=next(s for s in allof(w,unreal.Ship) if s.get_name()==ships[0].get_name())
  if label=='npc_entry':pos=unreal.Vector(drop.x-300,drop.y-300,500)
  elif label=='rain_cabin':pos=unreal.SWAudioAuthoringLibrary.find_audio_probe_cabin_point(ship)
  else:pos=ship.get_actor_location()+unreal.Vector(0,-1000,1800)
  cam.set_actor_location(pos,False,True)
  for pc in allof(w,unreal.PlayerController):
   pawn=pc.get_controlled_pawn()
   if pawn:pawn.get_editor_property('character_movement').disable_movement();pawn.set_actor_location(pos+unreal.Vector(0,0,100),False,True)
  npcs=[]
  for a in allof(w,unreal.Character):
   loc=a.get_actor_location();target=isinstance(a,unreal.BaseEnemy) and (loc.x-drop.x)**2+(loc.y-drop.y)**2<500**2 and loc.z>-10000
   fol=a.get_component_by_class(unreal.SWFoleyComponent)
   if fol:fol.set_editor_property('bEnabled',label=='npc_entry' and target)
   if target:
    ai=a.get_controller()
    if isinstance(ai,unreal.AIController):
     ai.stop_movement();brain=ai.get_editor_property('brain_component')
     if brain:brain.stop_logic('Replicated audio fall probe')
    names[w.get_path_name()]=a.get_name();npcs.append({'name':a.get_name(),'position':str(loc),'water_detection':fol.get_editor_property('bDetectWaterWithoutSwimming')})
  frame['worlds'].append({'world':unreal.SWAudioAuthoringLibrary.describe_audio_world(w),'cabin':d.get_editor_property('listener_in_cabin'),'listener':str(pos),'npc':npcs})
 if now-last_sample>=.2:last_sample=now;rows.append(frame)
result={'phases':phases,'drop':str(drop)}
