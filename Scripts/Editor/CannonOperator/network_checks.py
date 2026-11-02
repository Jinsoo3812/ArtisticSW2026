import json,math
exec((OUT.parent.parent/'LiveScripts/continuity.py').read_text(encoding='utf-8-sig'),globals())
f=live_api['cannon_test'];worlds=f['worlds'];pcs=f['pcs'];names=f['names'];server=worlds[0]
locals=[pc.get_controlled_pawn() for pc in pcs]
assert all(isinstance(c,unreal.Cannon) for c in locals)
sub=unreal.get_default_object(unreal.load_class(None,'/Script/Engine.SubsystemBlueprintLibrary'))
inputs=[sub.call_method('GetLocalPlayerSubSystemFromPlayerController',args=(pc,unreal.EnhancedInputLocalPlayerSubsystem.static_class())) for pc in pcs]
actions=[c.get_editor_property('cannon_look_action') for c in locals]
assert all(actions)
angles=live_api.get('cannon_validation_angles',[(0,0),(-60,-15),(-60,45),(0,45),(60,45),(60,-15),(0,-15),(0,0)]*2)
continuity=CannonContinuityProbe(worlds,names)
began=None;last=-1.;rows=[];done=False
def v(a):return [a.x,a.y,a.z]
def knee(a,b,c):
    u=a-b;q=c-b;den=max(.001,u.length()*q.length());return math.degrees(math.acos(max(-1.,min(1.,u.dot(q)/den))))
def on_tick(delta):
    global began,last,done
    if done:return
    now=unreal.GameplayStatics.get_time_seconds(server)
    if began is None:began=now
    phase=int((now-began)/3.)
    if phase>=len(angles):
        for inp,act in zip(inputs,actions):inp.inject_input_vector_for_action(act,unreal.Vector(),[],[])
        (OUT/'cannon_network_samples.json').write_text(json.dumps(rows,indent=2))
        (OUT/'cannon_network_complete.json').write_text(json.dumps({'angles':angles,'samples':len(rows),'owners':names},indent=2))
        continuity.save(OUT/'cannon_aim_continuity.json')
        done=True;return
    yaw,pitch=angles[phase]
    continuity.sample(now,str(phase))
    for inp,act,c in zip(inputs,actions,locals):
        current_yaw=c.get_editor_property('base_mesh').get_editor_property('relative_rotation').yaw
        current_pitch=c.get_editor_property('barrel_mesh').get_editor_property('relative_rotation').pitch
        inp.inject_input_vector_for_action(act,unreal.Vector((yaw-current_yaw)*.35,(pitch-current_pitch)*.35,0),[],[])
    if (now-began)%3.<1.5 or now-last<.1:return
    last=now;row={'phase':phase,'target_yaw':yaw,'target_pitch':pitch,'time':now,'worlds':[]}
    for w,pc in zip(worlds,pcs):
        wr={'world':unreal.SWAudioAuthoringLibrary.describe_audio_world(w),'operators':[]}
        for p in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.BasePlayer):
            if p.get_name() not in names:continue
            c=p.get_attach_parent_actor();assert isinstance(c,unreal.Cannon)
            mesh=p.get_editor_property('mesh');root=p.get_editor_property('root_component');rot=root.get_editor_property('relative_rotation')
            d={'name':p.get_name(),'cannon':c.get_name(),'hidden_for_this_view':c.is_operator_hidden_for_controller(pc),'own_operator':names.index(p.get_name())==worlds.index(w),'root_pitch':rot.pitch,'root_roll':rot.roll,'hand_errors':{},'foot_errors':{},'limb_lengths':{},'knee_angles':{},'bones':{}}
            for b in ['pelvis','upperarm_l','lowerarm_l','hand_l','upperarm_r','lowerarm_r','hand_r','thigh_l','calf_l','foot_l','thigh_r','calf_r','foot_r']:
                d['bones'][b]=v(mesh.get_socket_location(b))
            for bone,point in [('hand_l','left_hand_grip'),('hand_r','right_hand_grip'),('foot_l','left_foot_plant'),('foot_r','right_foot_plant')]:
                target=c.get_editor_property(point).get_world_location()
                error=unreal.SWCannonOperatorAuthoringLibrary.get_operator_contact_error(p,{'foot_l':0,'foot_r':1,'hand_l':2,'hand_r':3}[bone])
                (d['hand_errors'] if bone.startswith('hand') else d['foot_errors'])[bone]=error
            for side in ['l','r']:
                for upper,lower,end in [('upperarm_','lowerarm_','hand_'),('thigh_','calf_','foot_')]:
                    a,b,e=[mesh.get_socket_location(prefix+side) for prefix in [upper,lower,end]]
                    d['limb_lengths'][upper+side]=a.distance(b);d['limb_lengths'][lower+side]=b.distance(e)
                    if upper=='thigh_':d['knee_angles'][side]=knee(a,b,e)
            d['socket_target_errors']={};d['contact_positions']={};d['contact_actor_positions']={};d['contact_parents']={}
            for bone,key in [('hand_l','left_hand_grip'),('hand_r','right_hand_grip')]:
                grip=c.get_editor_property(key);surface=grip.get_attach_parent()
                d['contact_parents'][bone]=surface.get_name()
                d['socket_target_errors'][bone]=grip.get_world_location().distance(surface.get_socket_location(grip.get_attach_socket_name()))
                d['contact_positions'][bone]=v(unreal.MathLibrary.inverse_transform_location(c.get_actor_transform(),grip.get_world_location()))
                d['contact_actor_positions'][bone]=v(unreal.MathLibrary.inverse_transform_location(p.get_actor_transform(),grip.get_world_location()))
            d['aim_bone_quats']={};d['aim_bone_positions']={}
            for bone in ['pelvis','spine_01','spine_02','spine_03','head','upperarm_l','upperarm_r','spine_04','spine_05']:
                tf=mesh.get_socket_transform(bone,unreal.RelativeTransformSpace.RTS_ACTOR);q=tf.rotation
                d['aim_bone_positions'][bone]=v(tf.translation)
                d['aim_bone_quats'][bone]=[q.x,q.y,q.z,q.w]
            d['actual_yaw']=c.get_editor_property('base_mesh').get_editor_property('relative_rotation').yaw
            d['actual_pitch']=c.get_editor_property('barrel_mesh').get_editor_property('relative_rotation').pitch
            wr['operators'].append(d)
        row['worlds'].append(wr)
    rows.append(row)
result={'angles':angles,'duration':len(angles)*3}
