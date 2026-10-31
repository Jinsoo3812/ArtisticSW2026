"""Inspected Desktop FBX -> separate canonical export bind; original preserved."""
import bpy,json,math,os,hashlib
from mathutils import Matrix,Vector
BASE=r'C:\Users\I\Documents\GitHub\ArtisticSW2026\SourceArt\Characters\ChWoman'
src=bpy.data.objects['SKM_Player_Woman.001'];source=bpy.data.objects['root.001'];ref=bpy.data.objects['REF_Quinn']
assert not bpy.data.objects.get('Woman_EXPORT')
for p in source.pose.bones:p.matrix_basis.identity()
bpy.context.view_layer.update()
col=bpy.data.collections.new('01_EXPORT');bpy.context.scene.collection.children.link(col)
a=source.copy();a.data=source.data.copy();col.objects.link(a);a.name='Woman_EXPORT'
aw=a.matrix_world.copy();a.parent=None;a.data.transform(aw);a.matrix_world=Matrix.Identity(4)
m=src.copy();m.data=src.data.copy();col.objects.link(m);m.name='SKM_ChWoman_UE5'
mw=src.matrix_world.copy();m.parent=a;m.matrix_parent_inverse=Matrix.Identity(4);m.matrix_world=Matrix.Identity(4);m.data.transform(mw)
for mod in m.modifiers:
    if mod.type=='ARMATURE':mod.object=a;mod.use_deform_preserve_volume=False
original=[v.co.copy() for v in m.data.vertices]
rename={}
for side in ['l','r']:
    for old,new in [('upperarmtwist','upperarm_twist'),('forearmtwist','lowerarm_twist'),('thightwist','thigh_twist'),('calftwist','calf_twist')]:
        for i in [1,2]:rename[f'cc_base_{side}_{old}{i:02d}']=f'{new}_{i:02d}_{side}'
for old,new in rename.items():
    a.data.bones[old].name=new
    if m.vertex_groups.get(old):m.vertex_groups[old].name=new
bpy.ops.object.select_all(action='DESELECT');a.select_set(True);bpy.context.view_layer.objects.active=a
bpy.ops.object.mode_set(mode='EDIT');eb=a.data.edit_bones
positions={b.name:b.head.copy() for b in eb}
root=eb.new('root');root.head=(0,0,0);root.tail=(0,.08,0);root.use_deform=False
for rb in ref.data.bones:
    if rb.name not in eb:eb.new(rb.name).length=.05
for rb in ref.data.bones:
    b=eb[rb.name];b.use_connect=False;b.parent=eb[rb.parent.name] if rb.parent else root
    mat=(ref.matrix_world.to_quaternion()@rb.matrix_local.to_quaternion()).to_matrix().to_4x4()
    mat.translation=positions.get(rb.name,ref.matrix_world@rb.head_local)
    b.matrix=mat;b.length=max(.01,min(rb.length*.01,.08))
for side in ['l','r']:
    for stem,start,end in [('upperarm','upperarm','lowerarm'),('lowerarm','lowerarm','hand'),('thigh','thigh','calf'),('calf','calf','foot')]:
        for i,t in [(1,.5),(2,.75)]:
            b=eb[f'{stem}_twist_{i:02d}_{side}'];mat=b.matrix.copy();mat.translation=positions[start+'_'+side].lerp(positions[end+'_'+side],t);b.matrix=mat;b.length=.03
for marker,target in [('ik_foot_l','foot_l'),('ik_foot_r','foot_r'),('ik_hand_l','hand_l'),('ik_hand_r','hand_r'),('ik_hand_gun','hand_r')]:
    b=eb[marker];mat=b.matrix.copy();mat.translation=eb[target].head;b.matrix=mat
for b in eb:
    if b.name.startswith('ik_') or b.name in ['root','interaction','center_of_mass']:b.use_deform=False
bpy.ops.object.mode_set(mode='OBJECT');bpy.context.view_layer.update()
delta=max((v.co-original[v.index]).length for v in m.evaluated_get(bpy.context.evaluated_depsgraph_get()).data.vertices)
for o in list(bpy.context.scene.objects):
    if o not in [a,m]:o.hide_render=True;o.hide_set(True)
a.hide_render=False;a.hide_set(False);m.hide_render=False;m.hide_set(False)
images=[]
for mat in m.data.materials:
    if not mat.use_nodes:continue
    for n in mat.node_tree.nodes:
        if n.type!='TEX_IMAGE' or not n.image:continue
        image=n.image;name=image.filepath.replace('\\','/').split('/')[-1].rsplit('.',1)[0]+'.tga'
        filename=BASE+'/Textures/'+name
        assert os.path.exists(filename),filename
        image.filepath=filename;image.reload()
        if 'Normal' in name:image.colorspace_settings.name='Non-Color'
        image.pack();images.append(name)
audit={'source_vertices':len(src.data.vertices),'vertices':len(m.data.vertices),'bones':len(a.data.bones),'renamed_twists':rename,
       'canonical_rebind_surface_error_cm':delta*100,'packed_images':images,'shape_keys':0,
       'missing_core':sorted(set(ref.data.bones.keys())-set(a.data.bones.keys())),
       'parent_mismatches':[b.name for b in ref.data.bones if a.data.bones[b.name].parent.name!=(b.parent.name if b.parent else 'root')]}
open(BASE+'/Work/build_audit.json','w').write(json.dumps(audit,indent=2))
result=audit
