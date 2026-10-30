"""Add an NPC-only four-bone sash and bake gentle, loopable secondary motion.
Original 83 bones and original/reference actions are never edited.
"""
import bpy,json,math
from pathlib import Path
from mathutils import Matrix,Vector
from mathutils.kdtree import KDTree
ROOT=Path(__file__).resolve().parents[2]/'SourceArt/Characters/YiSunSin'
arm=bpy.data.objects['Armature'];work=bpy.data.objects['SK_YiSunSin_NPC'];ref=bpy.data.objects['REF_CompleteRootWeights']
scene=bpy.context.scene
names=['npc_sash_%02d'%i for i in range(1,5)]
original={b.name:(b.matrix_local.copy(),b.parent.name if b.parent else None) for b in arm.data.bones if b.name not in names}
assert len(original)==83
ids={v for p in work.data.polygons if p.material_index==0 for v in p.vertices}
parents={i:i for i in ids}
def find(i):
    while parents[i]!=i: parents[i]=parents[parents[i]];i=parents[i]
    return i
def union(a,b): parents[find(a)]=find(b)
for edge in work.data.edges:
    a,b=edge.vertices
    if a in ids and b in ids: union(a,b)
tree=KDTree(len(ids))
for i in ids: tree.insert(ref.data.vertices[i].co,i)
tree.balance()
for i in ids:
    for _,j,_ in tree.find_range(ref.data.vertices[i].co,1e-5): union(i,j)
components={}
for i in ids: components.setdefault(find(i),[]).append(i)
tails=set()
for indices in components.values():
    if min((ref.matrix_world@ref.data.vertices[i].co).z for i in indices)<.5: tails.update(indices)
assert len(tails)==1280
bpy.ops.object.select_all(action='DESELECT');arm.hide_set(False);arm.select_set(True);bpy.context.view_layer.objects.active=arm
bpy.ops.object.mode_set(mode='EDIT')
for i,name in enumerate(names):
    bone=arm.data.edit_bones.get(name) or arm.data.edit_bones.new(name)
    bone.head=arm.matrix_world.inverted()@Vector((-.07,-.225,1.25-.25*i))
    bone.tail=arm.matrix_world.inverted()@Vector((-.07,-.225,1.00-.25*i))
    bone.parent=arm.data.edit_bones[names[i-1] if i else 'spine']
    bone.use_connect=False
bpy.ops.object.mode_set(mode='OBJECT')
for name in names:
    if not work.vertex_groups.get(name):work.vertex_groups.new(name=name)
# Restore just the sash from the preserved mesh, so rerunning is deterministic.
for i in ids:
    work.data.vertices[i].co=ref.data.vertices[i].co
    for group in work.vertex_groups: group.remove([i])
    work.vertex_groups['spine'].add([i],1.0,'REPLACE')
for i in tails:
    point=ref.matrix_world@ref.data.vertices[i].co
    t=max(0,min(1,(1.25-point.z)/.10));anchor=t*t*(3-2*t)
    if not anchor:continue
    work.vertex_groups['spine'].add([i],1-anchor,'REPLACE')
    height=max(0,min(3,(1.25-point.z)/.25));a=int(height);b=min(3,a+1);blend=height-a
    work.vertex_groups[names[a]].add([i],anchor*(1-blend),'REPLACE')
    if a!=b:work.vertex_groups[names[b]].add([i],anchor*blend,'REPLACE')
    # A small authored front clearance protects the robe through blended poses.
    clearance=.085*max(0,min(1,(1.23-point.z)/.20))
    point.y-=clearance
    work.data.vertices[i].co=work.matrix_world.inverted()@point
work['npc_sash_blend_margin_m']=0.0
work.data.update()
clips=[('Idle_Upright','NPC_Idle_Upright',122,[3,2,1,1],[.3,.5,.6,.8]),
       ('Walk','NPC_Walk',36,[7,5,3,2],[1.4,2,2.8,3.2]),
       ('Run','NPC_Run',26,[11,7,4,3],[2,3,4,4.5])]
for label,source_name,end,bias,amplitude in clips:
    source=bpy.data.actions[source_name]
    source.use_fake_user=True
    target_name='NPC_Sash_'+label
    old=bpy.data.actions.get(target_name)
    if old: old.name=target_name+'_previous'
    action=source.copy();action.name=target_name;action.use_fake_user=True
    arm.animation_data.action=action;arm.animation_data.action_slot=action.slots[0]
    for frame in range(1,end+1):
        scene.frame_set(frame)
        phase=2*math.pi*(frame-1)/(end-1)
        for i,name in enumerate(names):
            pose=arm.pose.bones[name];pose.matrix_basis=Matrix.Identity(4)
        bpy.context.view_layer.update()
        for i,name in enumerate(names):
            pose=arm.pose.bones[name]
            world=arm.matrix_world@pose.matrix;pivot=world.translation
            angle=-(bias[i]+amplitude[i]*math.sin(phase-i*.55))
            side=(.25 if label=='Idle_Upright' else .6 if label=='Walk' else 1.0)*math.sin(phase-i*.7)
            rotation=Matrix.Rotation(math.radians(angle),4,'X')@Matrix.Rotation(math.radians(side),4,'Y')
            pose.matrix=arm.matrix_world.inverted()@Matrix.Translation(pivot)@rotation@Matrix.Translation(-pivot)@world
            bpy.context.view_layer.update()
            pose.rotation_mode='QUATERNION'
            for prop in ['location','rotation_quaternion','scale']:pose.keyframe_insert(data_path=prop,frame=frame,group=name)
        if label!='Idle_Upright':
            pose=arm.pose.bones[names[0]];world=arm.matrix_world@pose.matrix
            world.translation+=Vector((0,-.10,0));pose.matrix=arm.matrix_world.inverted()@world
            bpy.context.view_layer.update();pose.keyframe_insert(data_path='location',frame=frame,group=pose.name)
    action['npc_sash_forward_offset_m']=0.0 if label=='Idle_Upright' else .10
    action['npc_sash_motion']='Baked forward-biased four-bone chain; fixed knot; no runtime cloth solver'
for name,(matrix,parent) in original.items():
    bone=arm.data.bones[name]
    assert max(abs(matrix[r][c]-bone.matrix_local[r][c]) for r in range(4) for c in range(4))<1e-7
    assert (bone.parent.name if bone.parent else None)==parent
arm.animation_data.action=bpy.data.actions['NPC_Sash_Idle_Upright'];arm.animation_data.action_slot=arm.animation_data.action.slots[0]
scene.frame_start=1;scene.frame_end=122;scene.frame_set(1)
report={'new_bones':names,'original_bones_preserved':83,'npc_bones':len(arm.data.bones),'tail_vertices':len(tails),
        'fixed_knot_and_belt_vertices':len(ids-tails),'runtime_solver':False,'clips':[c[0] for c in clips],
        'front_clearance_max_m':.085,'original_mesh_and_actions_preserved':True}
(ROOT/'Audit/sash_authoring.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/'YiSunSin_NPC.blend'))
result=report
