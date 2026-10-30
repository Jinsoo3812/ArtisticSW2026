"""Exercise the same editable mesh socket used by the Socket Manager, then restore it."""
import json
f=live_api['cannon_test'];worlds=f['worlds'];server=worlds[0]
asset=unreal.load_asset('/Game/Blueprints/Ship/Cannon/Mesh/Operator/SM_OperatorBarrelVisual')
socket=asset.find_socket('Operator_LeftPalm')
p=socket.get_editor_property('relative_location');r=socket.get_editor_property('relative_rotation')
original=(p.x,p.y,p.z,r.pitch,r.yaw,r.roll)
began=None;changed=False;done=False

def restore():
 socket.set_editor_property('relative_location',unreal.Vector(*original[:3]))
 socket.set_editor_property('relative_rotation',unreal.Rotator(pitch=original[3],yaw=original[4],roll=original[5]))

def on_tick(delta):
 global began,changed,done
 if done:return
 now=unreal.GameplayStatics.get_time_seconds(server)
 if began is None:began=now
 if now-began<.5:return
 if not changed:
  socket.set_editor_property('relative_location',unreal.Vector(original[0],original[1],original[2]+1.))
  socket.set_editor_property('relative_rotation',unreal.Rotator(pitch=original[3],yaw=original[4]+2.,roll=original[5]))
  changed=True;return
 if now-began<1.5:return
 rows=[]
 try:
  for w in worlds:
   for actor in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.BasePlayer):
    if actor.get_name() not in f['names']:continue
    c=actor.get_attach_parent_actor();grip=c.get_editor_property('left_hand_grip')
    local=unreal.MathLibrary.inverse_transform_location(c.get_actor_transform(),grip.get_world_location())
    error=unreal.SWCannonOperatorAuthoringLibrary.get_operator_contact_error(actor,2)
    assert abs(local.z-original[2]-1.)<.01,(actor.get_name(),str(local),original)
    assert error<1.,(actor.get_name(),error)
    rows.append({'world':w.get_path_name(),'player':actor.get_name(),'z_offset_cm':local.z-original[2],'palm_error_cm':error})
 finally:restore()
 (OUT/'cannon_socket_adjustment.json').write_text(json.dumps({'offset_cm':1,'yaw_offset_deg':2,'actors':rows,'restored':True},indent=2))
 done=True
result={'requested':'edit left palm socket +1cm/+2deg and verify both sexes on both peers, then restore'}
