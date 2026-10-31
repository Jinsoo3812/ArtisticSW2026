import bpy,json
from mathutils import Vector
r=bpy.data.objects['Woman_CTRL'];a=bpy.data.objects['Woman_EXPORT'];m=bpy.data.objects['SKM_ChWoman_UE5'];points={}
for side in ['l','r']:
    foot=a.data.bones['foot_'+side].head_local.copy();ball=a.data.bones['ball_'+side].head_local.copy();forward=ball-foot;forward.z=0;forward.normalize()
    # Audited shoe vertices: source material slot 6, foot-side sign, low sole.
    indices={i for poly in m.data.polygons if poly.material_index==6 for i in poly.vertices}
    verts=[m.data.vertices[i].co.copy() for i in indices if m.data.vertices[i].co.x*(1 if side=='l' else -1)>0]
    sole=min(v.z for v in verts);low=[v for v in verts if v.z<sole+.025]
    heel=min(low,key=lambda v:(v-foot).dot(forward));toe=max(low,key=lambda v:(v-foot).dot(forward))
    heel.x=foot.x;toe.x=ball.x;points[side]={'foot':foot,'heel':heel,'toe':toe}
bpy.ops.object.select_all(action='DESELECT');r.select_set(True);bpy.context.view_layer.objects.active=r;bpy.ops.object.mode_set(mode='EDIT')
for side,positions in points.items():
    parent=r.data.edit_bones['FK_root']
    for name,key in [('FOOT_'+side,'foot'),('ROLL_heel_'+side,'heel'),('ROLL_toe_'+side,'toe')]:
        b=r.data.edit_bones.new(name);b.head=positions[key];b.tail=b.head+Vector((0,0,.08));b.parent=parent;b.use_deform=False;parent=b
    r.data.edit_bones['IK_leg_'+side].parent=parent
bpy.ops.object.mode_set(mode='OBJECT')
for side in points:
    for name in ['FOOT_'+side,'ROLL_heel_'+side,'ROLL_toe_'+side]:
        b=r.data.bones[name];r.data.collections['IK Goals'].assign(b);b.color.palette='THEME04' if side=='l' else 'THEME03'
        p=r.pose.bones[name];p.rotation_mode='XYZ';p.lock_scale=(True,True,True);p.custom_shape=bpy.data.objects['WGT_Goal'];p.use_custom_shape_bone_size=False;p.custom_shape_scale_xyz=(.07,.07,.07)
        if name.startswith('ROLL_'):p.lock_location=(True,True,True);p.lock_rotation=(False,True,True);p.custom_shape_scale_xyz=(.04,.04,.04)
bpy.context.view_layer.update()
result={side:{n:list(v) for n,v in ps.items()} for side,ps in points.items()}
open(r'C:\Users\I\Documents\GitHub\ArtisticSW2026\SourceArt\Characters\ChWoman\Work\foot_control_audit.json','w').write(json.dumps(result,indent=2))
