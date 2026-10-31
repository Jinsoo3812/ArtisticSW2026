import bpy,json,math
from mathutils import Vector,Quaternion
BASE=r'C:\Users\I\Documents\GitHub\ArtisticSW2026\SourceArt\Characters\ChMan'
a=bpy.data.objects['Armature'];m=bpy.data.objects['SKM_ChMan_UE5'];s=bpy.context.scene
original=list(m.data.materials)
for i,mat in enumerate(original):
    preview=bpy.data.materials.get(mat.name+'_Preview')
    if preview:m.data.materials[i]=preview
samples=[]
for angle,location,scale,label in [(60,(2,-4,1.0),1.5,'03_fixed_arms_raised'),(90,(2,-4,1.0),1.75,'04_fixed_arms_up_front'),(120,(2,4,1.0),1.75,'05_fixed_overhead_back'),(0,(2.5,-5,2.25),2.2,'06_fixed_rest')]:
    for pb in a.pose.bones:pb.matrix_basis.identity()
    for side,sign in [('l',-1),('r',1)]:
        p=a.pose.bones['upperarm_'+side];p.rotation_mode='QUATERNION'
        axis=p.bone.matrix_local.to_quaternion().inverted()@Vector((0,1,0))
        p.rotation_quaternion=Quaternion(axis,math.radians(sign*angle))
    bpy.context.view_layer.update()
    target=Vector((0,0,1.3 if angle else 1))
    s.camera.location=location;s.camera.rotation_euler=(target-s.camera.location).to_track_quat('-Z','Y').to_euler();s.camera.data.ortho_scale=scale
    s.render.filepath=BASE+'\\Review\\'+label+'.png'
    bpy.ops.render.render(write_still=True)
    error=max((a.pose.bones[x].matrix.translation-a.pose.bones[y].matrix.translation).length*100 for x,y in [('ik_hand_l','hand_l'),('ik_hand_r','hand_r'),('ik_foot_l','foot_l'),('ik_foot_r','foot_r')])
    samples.append({'arm_raise_from_rest_deg':angle,'max_limb_marker_error_cm':error,'render':label+'.png'})
for i,mat in enumerate(original):m.data.materials[i]=mat
for p in a.pose.bones:p.matrix_basis.identity()
bpy.context.view_layer.update()
weights=[sum(g.weight for g in v.groups) for v in m.data.vertices]
report={'samples':samples,'weight_sum_min':min(weights),'weight_sum_max':max(weights),'unweighted_vertices':sum(v<.00001 for v in weights),'max_influences':max(len(v.groups) for v in m.data.vertices),'root_head':list(a.data.bones['root'].head_local),'armature_transform':[list(r) for r in a.matrix_world],'mesh_transform':[list(r) for r in m.matrix_world]}
with open(BASE+'\\Work\\blender_pose_audit.json','w') as f:json.dump(report,f,indent=2)
bpy.ops.wm.save_as_mainfile(filepath=BASE+'\\ChMan_UE5_Rig.blend')
result=report
