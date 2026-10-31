"""Run through Blender MCP in the inspected source/reference scene.

Keep source objects and materials, create a separate UE5 export rig and skin.
No auto weights, shape scaling, or changes to the shared Unreal Skeleton.
"""
import bpy, bmesh, math, json, os
from mathutils import Vector, Matrix

BASE = r'C:\Users\I\Documents\GitHub\ArtisticSW2026\SourceArt\Characters\ChMan'
src = bpy.data.objects['ChManNewRig.001']
src_arm = bpy.data.objects['root']
ref = bpy.data.objects['REF_Manny']
for pb in src_arm.pose.bones:
    pb.matrix_basis.identity()
bpy.context.view_layer.update()
for i, name in enumerate(['M_ChManHair','M_ChManAccessory','M_ChManClothes','M_ChManStylizedBag','M_ChManEye','M_ChManShoes','M_ChManBody']):
    src.data.materials[i] = bpy.data.materials[name]

col = bpy.data.collections.new('ChMan UE5 Export')
bpy.context.scene.collection.children.link(col)
arm = src_arm.copy()
arm.data = src_arm.data.copy()
col.objects.link(arm)
arm.name = 'Armature'
world = src_arm.matrix_world.copy()
arm.parent = None
arm.matrix_world = world
bind_offset = world.to_3x3().inverted() @ world.translation
arm.location = (0,0,0)
bpy.context.view_layer.update()
mesh = src.copy()
mesh.data = src.data.copy()
col.objects.link(mesh)
mesh.name = 'SKM_ChMan_UE5'
world = src.matrix_world.copy()
mesh.parent = arm
mesh.matrix_world = world
for mod in mesh.modifiers:
    if mod.type == 'ARMATURE':
        mod.object = arm
        mod.use_deform_preserve_volume = False
for o in [src, src_arm, bpy.data.objects['ChManNewRig']]:
    o.hide_render = True
    o.hide_set(True)
mesh.hide_render = False
mesh.hide_set(False)
arm.hide_set(False)

# Reuse the existing weighted twist bones instead of empty cosmetic names.
rename = {}
for side in ['l', 'r']:
    for old, new in [('upperarmtwist','upperarm_twist'),('forearmtwist','lowerarm_twist'),('thightwist','thigh_twist'),('calftwist','calf_twist')]:
        for i in [1, 2]:
            rename[f'cc_base_{side}_{old}{i:02d}'] = f'{new}_{i:02d}_{side}'
for old, new in rename.items():
    arm.data.bones[old].name = new
    vg = mesh.vertex_groups.get(old)
    if vg:
        vg.name = new

bpy.ops.object.select_all(action='DESELECT')
arm.select_set(True)
bpy.context.view_layer.objects.active = arm
bpy.ops.object.mode_set(mode='EDIT')
eb = arm.data.edit_bones
root = eb.new('root')
root.head = (0,0,0)
root.tail = (0,10,0)
root.use_deform = False

old_positions = {b.name:b.head.copy() for b in eb}
old_matrices = {b.name:b.matrix.copy() for b in eb}
for rb in ref.data.bones:
    if rb.name not in eb:
        b = eb.new(rb.name)
        # Interaction markers have no weighted surface. Keep their native basis.
        b.matrix = rb.matrix_local.copy()
        b.length = max(rb.length, 1.0)
        b.use_deform = False
for rb in ref.data.bones:
    b = eb[rb.name]
    b.use_connect = False
    b.parent = eb[rb.parent.name] if rb.parent else root
    # Canonical joint orientation at the character's original joint location.
    # Rebinding the mesh preserves its original A-pose and body proportions.
    mat = rb.matrix_local.copy()
    if rb.name in old_positions:
        mat.translation = old_positions[rb.name]
    b.matrix = mat
    b.length = max(min(rb.length, 12.0), 1.0)

# Author the standard twist positions along the actual skin's limb segments.
for side in ['l','r']:
    for stem,start,end in [('upperarm','upperarm','lowerarm'),('lowerarm','lowerarm','hand'),('thigh','thigh','calf'),('calf','calf','foot')]:
        h0, h1 = old_positions[start+'_'+side], old_positions[end+'_'+side]
        for i,t in [(1,.5),(2,.75)]:
            b = eb[f'{stem}_twist_{i:02d}_{side}']
            mat = ref.data.bones[b.name].matrix_local.copy()
            mat.translation = h0.lerp(h1,t)
            b.matrix = mat
            b.length = 3

# Standard unweighted end markers must describe this mesh, not another rig.
for marker, target in [('ik_foot_l','foot_l'),('ik_foot_r','foot_r'),('ik_hand_l','hand_l'),('ik_hand_r','hand_r'),('ik_hand_gun','hand_r')]:
    mat = eb[marker].matrix.copy()
    mat.translation = eb[target].head
    eb[marker].matrix = mat
    eb[marker].use_deform = False
for b in eb:
    if b.name not in ['root','ik_foot_root','ik_hand_root','interaction','center_of_mass']:
        b.translate(bind_offset)
bpy.ops.object.mode_set(mode='OBJECT')

# A separate overlapping skin inset extends the trimmed upper-arm boundary
# under the sleeve. Existing UVs, polygons and vertex weights remain intact.
for name in ['upperarm_l','upperarm_r','clavicle_l','clavicle_r','spine_05']:
    if not mesh.vertex_groups.get(name):
        mesh.vertex_groups.new(name=name)
bm = bmesh.new()
bm.from_mesh(mesh.data)
skinverts = [v for v in bm.verts if any(f.material_index == 6 for f in v.link_faces)]
bmesh.ops.remove_doubles(bm, verts=skinverts, dist=.0001)
bm.verts.ensure_lookup_table()
deform = bm.verts.layers.deform.active
source_uv = bm.loops.layers.uv.active
boundaries = []
seen = set()
for edge in bm.edges:
    if not edge.is_boundary or edge in seen or not all(f.material_index == 6 for f in edge.link_faces):
        continue
    stack, edges = [edge], []
    while stack:
        e = stack.pop()
        if e in seen:
            continue
        seen.add(e)
        edges.append(e)
        for v in e.verts:
            stack.extend(q for q in v.link_edges if q.is_boundary and q not in seen)
    vs = set(v for e in edges for v in e.verts)
    center = sum((v.co for v in vs),Vector()) / len(vs)
    if len(edges) == 19 and 118 < center.z < 132 and 20 < abs(center.x) < 36:
        ordered = [edges[0].verts[0]]
        previous = None
        while len(ordered) < len(vs):
            v = ordered[-1]
            candidates = [q.other_vert(v) for q in v.link_edges if q in edges and q.other_vert(v) != previous]
            nxt = next((q for q in candidates if q not in ordered),None)
            if nxt is None:
                break
            previous = v
            ordered.append(nxt)
        assert len(ordered) == 19
        boundaries.append((center, ordered))
assert len(boundaries) == 2, 'Expected exactly the two audited upper-arm openings'

verts, faces, weights, uvs = [], [], [], []
patch_audit = []
for center, ring in boundaries:
    side = 'l' if center.x > 0 else 'r'
    shoulder = arm.data.bones['upperarm_'+side].head_local.copy()
    # Recess the proximal end inside the torso, not above the shoulder surface.
    proximal = shoulder + Vector((-6 if side=='l' else 6, -2, -7))
    start = len(verts)
    for j,t in enumerate([0,.3,.65,1]):
        c = center.lerp(proximal,t)
        for k,v in enumerate(ring):
            co = c + (v.co-center)*(1-.65*t)
            verts.append(tuple(co))
            w = dict(v[deform])
            # Preserve identical motion at the existing skin edge; blend only
            # the deeply hidden shoulder inset toward upperarm/clavicle/chest.
            if t > 0:
                mix = .30*t
                w = {g:value*(1-mix) for g,value in w.items()}
                for name,value in [('upperarm_'+side,.55),('clavicle_'+side,.25),('spine_05',.20)]:
                    gi = mesh.vertex_groups[name].index
                    w[gi] = w.get(gi,0) + mix*value
            weights.append(w)
            skinloops = [loop for loop in v.link_loops if loop.face.material_index == 6]
            base_uv = skinloops[0][source_uv].uv.copy()
            loop = skinloops[0]
            neighbor = next((q for q in [loop.link_loop_next,loop.link_loop_prev] if q.vert not in ring),loop.link_loop_next)
            distance = max((v.co-neighbor.vert.co).length, .5)
            direction = base_uv-neighbor[source_uv].uv
            uvs.append(tuple(base_uv+direction*(t*(proximal-center).length/distance)))
    n = len(ring)
    for j in range(3):
        for k in range(n):
            kn = (k+1)%n
            faces.append((start+j*n+k,start+j*n+kn,start+(j+1)*n+kn,start+(j+1)*n+k))
    faces.append(tuple(start+3*n+k for k in reversed(range(n))))
    patch_audit.append({'side':side,'boundary_vertices':n,'original_center_cm':list(center),'inset_center_cm':list(proximal)})
bm.free()
data = bpy.data.meshes.new('ChMan_UnderSleeveSkin')
data.from_pydata(verts,[],faces)
data.update()
patch = bpy.data.objects.new('ChMan_UnderSleeveSkin',data)
col.objects.link(patch)
patch.parent = arm
patch.matrix_world = mesh.matrix_world.copy()
for vg in mesh.vertex_groups:
    patch.vertex_groups.new(name=vg.name)
for vi,w in enumerate(weights):
    total = sum(w.values())
    for gi,value in w.items():
        if value > 0:
            patch.vertex_groups[gi].add([vi],value/total,'REPLACE')
data.materials.append(bpy.data.materials['M_ChManBody'])
uv = data.uv_layers.new(name='UVMap')
# UV continuation uses the nearest original skin boundary loop per new vertex.
# Copy existing skin UV values for ring zero and extend inside the hidden sleeve.
for poly in data.polygons:
    poly.use_smooth = True
    for li in poly.loop_indices:
        uv.data[li].uv = uvs[data.loops[li].vertex_index]
pbm = bmesh.new();pbm.from_mesh(data)
bmesh.ops.recalc_face_normals(pbm,faces=list(pbm.faces))
bmesh.ops.triangulate(pbm,faces=[f for f in pbm.faces if len(f.verts)>4])
pbm.to_mesh(data);pbm.free()
mod = patch.modifiers.new('UE5 Skin','ARMATURE');mod.object=arm
mod.use_deform_preserve_volume=False

# Join the inset without welding/reindexing the original surface.
bpy.ops.object.select_all(action='DESELECT')
mesh.select_set(True);patch.select_set(True)
bpy.context.view_layer.objects.active=mesh
bpy.ops.object.join()

# Bake the imported centimetre object transforms into the export datablocks.
# Both export objects now have identity transforms; the explicit root is at
# world origin and FBX bind-pose parent/child matrices share one space.
bpy.context.view_layer.update()
mesh_world=mesh.matrix_world.copy()
arm_world=arm.matrix_world.copy()
mesh.data.transform(mesh_world)
arm.data.transform(arm_world)
arm.matrix_world=Matrix.Identity(4)
mesh.matrix_parent_inverse=Matrix.Identity(4)
mesh.matrix_world=Matrix.Identity(4)
bpy.context.view_layer.update()

# Keep static bind markers in the exported reference; constraints make authoring
# previews track limbs. Export animation bake would sample these constraints.
for marker,target in [('ik_foot_l','foot_l'),('ik_foot_r','foot_r'),('ik_hand_l','hand_l'),('ik_hand_r','hand_r'),('ik_hand_gun','hand_r')]:
    c=arm.pose.bones[marker].constraints.new('COPY_TRANSFORMS')
    c.name='Follow '+target+' (authoring)'
    c.target=arm;c.subtarget=target;c.owner_space='WORLD';c.target_space='WORLD'
    c.mix_mode='REPLACE'
for b in arm.data.bones:
    if b.name.startswith('ik_') or b.name in ['root','interaction','center_of_mass']:
        b.use_deform=False

bpy.context.view_layer.update()
shared = set(ref.data.bones.keys())
audit = {
    'source':r'C:\Users\I\Desktop\ChManNewRig.FBX',
    'reference':'UE 5.7.4 /Game/Characters/Mannequins/Meshes/SKM_Manny_Simple',
    'source_vertices':len(src.data.vertices),'output_vertices':len(mesh.data.vertices),
    'source_polygons':len(src.data.polygons),'output_polygons':len(mesh.data.polygons),
    'output_bones':len(arm.data.bones),'core_bones_including_root':len(shared)+1,
    'renamed_weighted_twists':rename,'skin_insets':patch_audit,
    'missing_core':sorted(shared-set(arm.data.bones.keys())),
    'parent_mismatches':[n for n in shared if (arm.data.bones[n].parent.name if arm.data.bones[n].parent else None)!=(ref.data.bones[n].parent.name if ref.data.bones[n].parent else 'root')],
    'max_core_basis_angle_deg':max(math.degrees(arm.data.bones[n].matrix_local.to_quaternion().rotation_difference(ref.data.bones[n].matrix_local.to_quaternion()).angle) for n in shared),
}
orig_eval=src.evaluated_get(bpy.context.evaluated_depsgraph_get())
new_eval=mesh.evaluated_get(bpy.context.evaluated_depsgraph_get())
audit['original_surface_max_rest_delta_cm']=max(((src.matrix_world@v.co)-(mesh.matrix_world@new_eval.data.vertices[v.index].co)).length*100 for v in orig_eval.data.vertices)
os.makedirs(BASE+'\\FBX',exist_ok=True)
with open(BASE+'\\Work\\build_audit.json','w',encoding='utf8') as f:json.dump(audit,f,indent=2)
bpy.ops.object.select_all(action='DESELECT');arm.select_set(True);mesh.select_set(True);bpy.context.view_layer.objects.active=arm
unit_scale=bpy.context.scene.unit_settings.scale_length
try:
    # Emit actual centimetre data with unit object/root scales. Merely putting
    # 100 in an FBX root/object scale imports at the right height but breaks
    # animation when the animation's identity root scale replaces it.
    mesh.data.transform(Matrix.Scale(100,4))
    arm.data.transform(Matrix.Scale(100,4))
    bpy.context.scene.unit_settings.scale_length=.01
    bpy.context.view_layer.update()
    bpy.ops.export_scene.fbx(filepath=BASE+'\\FBX\\SKM_ChMan_UE5.fbx',use_selection=True,object_types={'ARMATURE','MESH'},axis_forward='-Y',axis_up='Z',apply_unit_scale=True,apply_scale_options='FBX_SCALE_NONE',add_leaf_bones=False,use_armature_deform_only=False,bake_anim=False,mesh_smooth_type='FACE',use_tspace=True,path_mode='STRIP',armature_nodetype='NULL',primary_bone_axis='Y',secondary_bone_axis='X')
finally:
    mesh.data.transform(Matrix.Scale(.01,4))
    arm.data.transform(Matrix.Scale(.01,4))
    bpy.context.scene.unit_settings.scale_length=unit_scale
    bpy.context.view_layer.update()
bpy.ops.wm.save_as_mainfile(filepath=BASE+'\\ChMan_UE5_Rig.blend')
result=audit
