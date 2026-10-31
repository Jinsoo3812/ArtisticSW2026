"""Bind only the separately materialed eye surfaces; no invented jaw skinning."""
import bpy,json
from mathutils import Vector
m=bpy.data.objects['SKM_ChWoman_UE5'];a=bpy.data.objects['Woman_EXPORT'];r=bpy.data.objects['Woman_CTRL']
ids={i for p in m.data.polygons if p.material_index==5 for i in p.vertices}
others={i for p in m.data.polygons if p.material_index!=5 for i in p.vertices}
assert not ids&others,'Eye surfaces must not share body vertices'
groups={side:m.vertex_groups.get('cc_base_'+side+'_eye') or m.vertex_groups.new(name='cc_base_'+side+'_eye') for side in ['l','r']}
counts={'l':0,'r':0}
for i in ids:
    v=m.data.vertices[i];side=min(['l','r'],key=lambda side:(v.co-a.data.bones['cc_base_'+side+'_eye'].head_local).length_squared)
    for g in list(v.groups):m.vertex_groups[g.group].remove([i])
    groups[side].add([i],1.0,'REPLACE');counts[side]+=1
bpy.ops.object.select_all(action='DESELECT');r.select_set(True);bpy.context.view_layer.objects.active=r;bpy.ops.object.mode_set(mode='EDIT')
b=r.data.edit_bones.new('LOOK_eyes');b.head=(a.data.bones['cc_base_l_eye'].head_local+a.data.bones['cc_base_r_eye'].head_local)/2+Vector((0,-.2,0));b.tail=b.head+Vector((0,0,.06));b.parent=r.data.edit_bones['FK_head'];b.use_deform=False
bpy.ops.object.mode_set(mode='OBJECT')
r.data.collections['Face'].assign(r.data.bones['LOOK_eyes']);r.data.collections['Face'].is_visible=True
p=r.pose.bones['LOOK_eyes'];p.rotation_mode='XYZ';p.lock_location=(True,True,True);p.lock_scale=(True,True,True);p.custom_shape=bpy.data.objects['WGT_Goal'];p.use_custom_shape_bone_size=False;p.custom_shape_scale_xyz=(.055,.04,.04)
for side in ['l','r']:
    p=r.pose.bones['FK_cc_base_'+side+'_eye'];c=p.constraints.new('COPY_ROTATION');c.name='Common eye rotation';c.target=r;c.subtarget='LOOK_eyes';c.target_space='LOCAL';c.owner_space='LOCAL';c.mix_mode='BEFORE'
refs=r.data.collections.new('Face Reference (unweighted)');refs.is_visible=False
for b in list(r.data.collections['Face'].bones):
    if b.name not in ['LOOK_eyes','FK_cc_base_l_eye','FK_cc_base_r_eye']:r.data.collections['Face'].unassign(b);refs.assign(b)
r.update_tag();bpy.context.view_layer.update()
result={'eye_vertices':len(ids),'per_side':counts,'new_face_weights':['cc_base_l_eye','cc_base_r_eye'],'jaw_lips_morphs':'not authored; original facial reference bones preserved'}
open(r'C:\Users\I\Documents\GitHub\ArtisticSW2026\SourceArt\Characters\ChWoman\Work\eye_binding_audit.json','w').write(json.dumps(result,indent=2))
