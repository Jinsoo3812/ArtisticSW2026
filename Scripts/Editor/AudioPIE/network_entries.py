import json
OUT=OUT/'EntriesVerified'
OUT.mkdir(exist_ok=True)
f=live_api['network_fixture'];worlds=f['worlds'];server=f['server']
pcs=f['allof'](server,unreal.PlayerController)
names=[pc.get_controlled_pawn().get_name() for pc in pcs]
phase=-1;began=0.;closing=0.;second_stopped=False;done=False;dropped=False;rows=[];last_sample=0.
origin=f['ships'][0].get_actor_location();drop=unreal.Vector(origin.x-2500,origin.y+5500,1200)
def stop(i):
 unreal.AudioMixerLibrary.stop_recording_output(worlds[i],unreal.AudioRecordingExportType.WAV_FILE,'network_verified_character'+str(phase)+'_entry_device'+str(i),str(OUT),None,None)
def on_tick(delta):
 global phase,began,closing,second_stopped,done,dropped,last_sample
 if done:return
 now=unreal.GameplayStatics.get_time_seconds(server);advance=phase<0
 for ship in f['ships']:
  ship.get_editor_property('BuoyancyRoot').set_physics_linear_velocity(unreal.Vector(),False)
 if phase>=0 and not closing and now-began>=10:
  stop(0);closing=now;second_stopped=False
 if closing and not second_stopped and now-closing>=.6:stop(1);second_stopped=True
 if closing and now-closing>=1.2:advance=True;closing=0.
 if advance:
  phase+=1
  if phase>=len(names):
   done=True;(OUT/'network_character_entries.json').write_text(json.dumps(rows,indent=2));unreal.log('SW_NETWORK_ENTRIES_DONE');return
  began=now;dropped=False
  for w in worlds:
   for p in f['allof'](w,unreal.Character):
    fol=p.get_component_by_class(unreal.SWFoleyComponent)
    if fol:fol.set_editor_property('bEnabled',p.get_name()==names[phase])
   unreal.AudioMixerLibrary.start_recording_output(w,15,None)
  unreal.log('SW_NETWORK_ENTRY_PHASE '+names[phase])
 for cam in f['cameras']:cam.set_actor_location(unreal.Vector(drop.x-350,drop.y-350,450),False,True)
 if now-began>=2 and not dropped:
  for w in worlds:
   for p in f['allof'](w,unreal.Character):
    if p.get_name()!=names[phase]:continue
    p.set_actor_location(drop,False,True)
    if p.get_controller():
     mov=p.get_editor_property('character_movement');mov.set_movement_mode(unreal.MovementMode.MOVE_FALLING,0)
  dropped=True
 if now-last_sample>.15:
  last_sample=now
  rows.append({'phase':names[phase],'time':now,'worlds':[{'description':unreal.SWAudioAuthoringLibrary.describe_audio_world(w),'players':[{'name':p.get_name(),'position':str(p.get_actor_location()),'swimming':p.get_component_by_class(unreal.SwimmingComponent).is_custom_swimming(),'shallow':p.get_component_by_class(unreal.SwimmingComponent).is_in_shallow_water()} for p in f['allof'](w,unreal.Character) if p.get_name() in names]} for w in worlds]})
result={'actors':names,'drop':str(drop)}
