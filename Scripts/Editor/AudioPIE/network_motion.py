import json
OUT=OUT/'MotionVerified'
OUT.mkdir(exist_ok=True)
f=live_api['network_fixture'];worlds=f['worlds'];server=f['server'];ships=f['ships']
phases=[('idle',5,0,0),('player',8,250,0),('enemy',8,0,250),('both',8,250,250),('stopped',6,0,0),('far',6,250,250),('returned',7,250,250)]
phase=-1;began=0.;done=False;last_sample=0.;rows=[];positions={};closing=0.;second_stopped=False
def save_record(label,i):
 unreal.AudioMixerLibrary.stop_recording_output(worlds[i],unreal.AudioRecordingExportType.WAV_FILE,'network_verified_'+label+'_device'+str(i),str(OUT),None,None)
def on_tick(delta):
 global phase,began,done,last_sample,closing,second_stopped
 if done:return
 now=unreal.GameplayStatics.get_time_seconds(server)
 advance=phase<0
 if phase>=0 and not closing and now-began>=phases[phase][1]:
  save_record(phases[phase][0],0);closing=now;second_stopped=False
 if closing and not second_stopped and now-closing>=.6:
  save_record(phases[phase][0],1);second_stopped=True
 if closing and now-closing>=1.2:
  advance=True;closing=0.
 if advance:
  phase+=1
  if phase>=len(phases):
   done=True
   for ship in ships:ship.get_editor_property('BuoyancyRoot').set_physics_linear_velocity(unreal.Vector(),False)
   (OUT/'network_motion_samples.json').write_text(json.dumps(rows,indent=2));unreal.log('SW_NETWORK_MOTION_DONE');return
  began=now
  for w in worlds:unreal.AudioMixerLibrary.start_recording_output(w,15,None)
  unreal.log('SW_NETWORK_MOTION_PHASE '+phases[phase][0])
 label,_,v1,v2=phases[phase]
 for ship,speed in zip(ships,[v1,v2]):
  root=ship.get_editor_property('BuoyancyRoot');v=root.get_physics_linear_velocity();root.set_physics_linear_velocity(unreal.Vector(speed,0,0),False);root.set_physics_angular_velocity_in_degrees(unreal.Vector(),False)
 p1=ships[0].get_actor_location();p2=ships[1].get_actor_location()
 far=20000 if label=='far' else 0
 center=unreal.Vector((p1.x+p2.x)/2+far,(p1.y+p2.y)/2,p1.z+1100)
 for cam in f['cameras']:cam.set_actor_location(center,False,True)
 for j,pc in enumerate(f['allof'](server,unreal.PlayerController)):
  pawn=pc.get_controlled_pawn()
  if pawn:pawn.set_actor_location(unreal.Vector(center.x-600-j*200,center.y,center.z),False,True)
 if now-last_sample>=.5:
  last_sample=now
  rows.append({'phase':label,'time':now,'ships':[str(s.get_actor_location()) for s in ships],'worlds':[{'description':unreal.SWAudioAuthoringLibrary.describe_audio_world(w),'audio':unreal.get_default_object(unreal.load_class(None,'/Script/Engine.SubsystemBlueprintLibrary')).call_method('GetWorldSubsystem',args=(w,unreal.SWShipAudioSubsystem.static_class())).get_audio_state(),'ships':[{'name':s.get_name(),'hidden':s.get_editor_property('hidden'),'pos':str(s.get_actor_location())} for s in f['allof'](w,unreal.Ship) if not s.get_editor_property('hidden')]} for w in worlds]})
result={'phases':phases,'devices':[unreal.SWAudioAuthoringLibrary.describe_audio_world(w) for w in worlds]}
