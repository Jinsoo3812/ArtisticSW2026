"""Match the actual mesh/bind limb directions to project Quinn's A-pose.

Run after build_chman_ue5.py, or on its inspected output. Keep anatomy lengths;
bake linear skin deformation, then restore canonical axes at posed joint heads.
"""
import bpy, json, math
from mathutils import Matrix, Vector
BASE=r'C:\Users\I\Documents\GitHub\ArtisticSW2026\SourceArt\Characters\ChWoman'
a=bpy.data.objects['Woman_EXPORT'];m=bpy.data.objects['SKM_ChWoman_UE5'];ref=bpy.data.objects['REF_Quinn']
assert a.matrix_world.is_identity and m.matrix_world.is_identity
assert len(m.data.vertices)==54393 and len(a.data.bones)==120
for p in a.pose.bones:p.matrix_basis.identity()
constraints=[(c,c.mute) for p in a.pose.bones for c in p.constraints]
for c,_ in constraints:c.mute=True
bpy.context.view_layer.update()
old_mesh=[v.co.copy() for v in m.data.vertices]
pairs=[]
for side in ['l','r']:
    pairs.extend([(x+'_'+side,y+'_'+side) for x,y in [('clavicle','upperarm'),('upperarm','lowerarm'),('lowerarm','hand'),('hand','middle_01')]])
    for digit in ['index','middle','ring','pinky','thumb']:
        pairs.extend([(f'{digit}_01_{side}',f'{digit}_02_{side}'),(f'{digit}_02_{side}',f'{digit}_03_{side}')])
    pairs.extend([(f'thigh_{side}',f'calf_{side}'),(f'calf_{side}',f'foot_{side}')])
old_lengths={x:(a.data.bones[y].head_local-a.data.bones[x].head_local).length for x,y in pairs}
old_foot_rot={side:a.data.bones['foot_'+side].matrix_local.to_quaternion() for side in ['l','r']}
audit={'pairs':[],'method':'Pose actual limbs to Quinn anatomical directions, bake LBS mesh, rebind with canonical axes; preserve limb lengths and world foot orientation.'}
for x,y in pairs:
    p=a.pose.bones[x]
    current=a.pose.bones[y].head-p.head
    desired=(ref.matrix_world@ref.data.bones[y].head_local)-(ref.matrix_world@ref.data.bones[x].head_local)
    before=math.degrees(current.angle(desired))
    delta=current.rotation_difference(desired)
    p.matrix=Matrix.LocRotScale(p.head.copy(),delta@p.matrix.to_quaternion(),Vector((1,1,1)))
    bpy.context.view_layer.update()
    after=math.degrees((a.pose.bones[y].head-p.head).angle(desired))
    audit['pairs'].append({'bone':x,'end':y,'before_deg':before,'posed_direction_error_deg':after})
for side in ['l','r']:
    p=a.pose.bones['foot_'+side]
    p.matrix=Matrix.LocRotScale(p.head.copy(),old_foot_rot[side],Vector((1,1,1)))
    bpy.context.view_layer.update()
evaluated=m.evaluated_get(bpy.context.evaluated_depsgraph_get())
posed_mesh=[v.co.copy() for v in evaluated.data.vertices]
posed_bones={p.name:p.matrix.copy() for p in a.pose.bones}
old_display_lengths={b.name:b.length for b in a.data.bones}
floor_shift=-min(v.z for v in posed_mesh)
audit['floor_shift_cm']=floor_shift*100
for co in posed_mesh:co.z+=floor_shift
for name,mat in posed_bones.items():
    if name not in ['root','ik_foot_root','ik_hand_root','interaction','center_of_mass']:
        mat.translation.z+=floor_shift
for v,co in zip(m.data.vertices,posed_mesh):v.co=co
for p in a.pose.bones:p.matrix_basis.identity()
bpy.ops.object.select_all(action='DESELECT');a.select_set(True);bpy.context.view_layer.objects.active=a
bpy.ops.object.mode_set(mode='EDIT')
for b in a.data.edit_bones:
    b.use_connect=False
    matrix=posed_bones[b.name].copy()
    rb=ref.data.bones.get(b.name)
    if rb:
        rotation=ref.matrix_world.to_quaternion()@rb.matrix_local.to_quaternion()
        matrix=rotation.to_matrix().to_4x4()
        matrix.translation=posed_bones[b.name].translation
    b.matrix=matrix;b.length=old_display_lengths[b.name]
for marker,target in [('ik_foot_l','foot_l'),('ik_foot_r','foot_r'),('ik_hand_l','hand_l'),('ik_hand_r','hand_r'),('ik_hand_gun','hand_r')]:
    b=a.data.edit_bones[marker];matrix=b.matrix.copy();matrix.translation=a.data.edit_bones[target].head;b.matrix=matrix
bpy.ops.object.mode_set(mode='OBJECT')
for c,mute in constraints:c.mute=mute
bpy.context.view_layer.update()
final=m.evaluated_get(bpy.context.evaluated_depsgraph_get())
audit['rebinding_max_surface_error_cm']=max((v.co-co).length*100 for v,co in zip(final.data.vertices,posed_mesh))
audit['max_bone_length_change_cm']=max(abs((a.data.bones[y].head_local-a.data.bones[x].head_local).length-old_lengths[x])*100 for x,y in pairs)
audit['max_final_direction_error_deg']=max(math.degrees((a.data.bones[y].head_local-a.data.bones[x].head_local).angle((ref.matrix_world@ref.data.bones[y].head_local)-(ref.matrix_world@ref.data.bones[x].head_local))) for x,y in pairs)
audit['max_intentional_surface_movement_cm']=max((v.co-old).length*100 for v,old in zip(m.data.vertices,old_mesh))
audit['floor_min_z_cm']=min(v.co.z for v in final.data.vertices)*100
with open(BASE+'\\Work\\quinn_apose_audit.json','w') as f:json.dump(audit,f,indent=2)
result={k:v for k,v in audit.items() if k!='pairs'}
