import json
OUT=OUT/'CannonsVerified'
OUT.mkdir(exist_ok=True)
f=live_api['network_fixture'];worlds=f['worlds'];server=f['server'];ships=f['ships']
for w in worlds:
 for p in f['allof'](w,unreal.Character):
  fol=p.get_component_by_class(unreal.SWFoleyComponent)
  if fol:fol.set_editor_property('bEnabled',False)
  p.get_editor_property('character_movement').disable_movement()
cases=[]
for ship,label in zip(ships,['player','enemy']):
 cs=[c for c in f['allof'](server,unreal.Cannon) if c.get_attach_parent_actor()==ship and c.can_fire_cannon()]
 assert cs,ship.get_path_name()
 other=next(s for s in ships if s!=ship);away=ship.get_actor_location()-other.get_actor_location()
 cs.sort(key=lambda c:c.get_actor_forward_vector().x*away.x+c.get_actor_forward_vector().y*away.y,reverse=True)
 cases.append((ship,cs[0],label))
phase=-1;began=0.;fired=False;done=False;rows=[];last_sample=0.;closing=0.;second_stopped=False
def stop(i):
 unreal.AudioMixerLibrary.stop_recording_output(worlds[i],unreal.AudioRecordingExportType.WAV_FILE,'network_verified_'+cases[phase][2]+'_cannon_device'+str(i),str(OUT),None,None)
def on_tick(delta):
 global phase,began,fired,done,last_sample,closing,second_stopped
 if done:return
 now=unreal.GameplayStatics.get_time_seconds(server)
 advance=phase<0
 if phase>=0 and not closing and now-began>=13:
  stop(0);closing=now;second_stopped=False
 if closing and not second_stopped and now-closing>=.6:
  stop(1);second_stopped=True
 if closing and now-closing>=1.2:
  advance=True;closing=0.
 if advance:
  phase+=1
  if phase==len(cases):
   done=True;(OUT/'network_cannon_results.json').write_text(json.dumps(rows,indent=2));unreal.log('SW_NETWORK_CANNONS_DONE');return
  began=now;fired=False
  for w in worlds:unreal.AudioMixerLibrary.start_recording_output(w,18,None)
  unreal.log('SW_NETWORK_CANNON_PHASE '+cases[phase][2])
 ship,c,label=cases[phase];p=c.get_actor_location()
 for s in ships:
  root=s.get_editor_property('BuoyancyRoot');v=root.get_physics_linear_velocity();root.set_physics_linear_velocity(unreal.Vector(),False);root.set_physics_angular_velocity_in_degrees(unreal.Vector(),False)
 for cam in f['cameras']:
  cam.set_actor_location(unreal.Vector(p.x-500,p.y-500,p.z+300),False,True)
  cam.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(cam.get_actor_location(),p),False)
 for j,pc in enumerate(f['allof'](server,unreal.PlayerController)):
  if pc.get_controlled_pawn():pc.get_controlled_pawn().set_actor_location(unreal.Vector(p.x-500-j*200,p.y-500,p.z+500),False,True)
 if now-began>=4 and not fired:
  queued=unreal.SWAudioAuthoringLibrary.queue_audio_cannon_shot(c,label=='enemy')
  rows.append({'phase':label,'time':now,'queued_on_native_world_timer':queued,'cannon':c.get_path_name(),'actual_result':'SW_NATIVE_AUDIO_SHOT log verifies accepted/cooldown'})
  unreal.log('SW_NETWORK_CANNON_FIRED '+json.dumps(rows[-1]));fired=True
result={'cases':[(s.get_path_name(),c.get_name(),label) for s,c,label in cases]}
