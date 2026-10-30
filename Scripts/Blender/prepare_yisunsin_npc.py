"""Run in Blender after importing the untouched UE reference mesh and Walk FBX.

Reconstruct UE's weighted Root node, which Blender represents as an object,
before evaluating skin or editing garment weights. Keep the raw import intact.
"""
import bpy
import json
from pathlib import Path
from mathutils import Matrix, Vector

ROOT = Path(__file__).resolve().parents[2] / "SourceArt/Characters/YiSunSin"
raw_mesh = bpy.data.objects['SK_YiSunSin_Latest.001']
raw_arm = bpy.data.objects['spine']
assert not bpy.data.objects.get('SK_YiSunSin_NPC'), 'Working copy already exists'
col = bpy.data.collections.new('03_NPC_Working')
bpy.context.scene.collection.children.link(col)
arm = raw_arm.copy()
arm.data = raw_arm.data.copy()
arm.name = 'Armature'
col.objects.link(arm)
arm.parent = None
arm.matrix_world = raw_arm.matrix_world.copy()
arm.animation_data_clear()
bpy.ops.object.select_all(action='DESELECT')
arm.hide_set(False)
arm.select_set(True)
bpy.context.view_layer.objects.active = arm
bpy.ops.object.mode_set(mode='EDIT')
original_roots = [b for b in arm.data.edit_bones if b.parent is None]
root = arm.data.edit_bones.new('spine')
root.head = (0, 0, 0)
root.tail = (0, 0.08, 0)
for bone in original_roots:
    bone.parent = root
# Match the animation export's authoring flags; the mesh-only import connects
# thumb_03_R and would ignore its local translation track.
arm.data.edit_bones['thumb_03_R'].use_connect = False
bpy.ops.object.mode_set(mode='OBJECT')

baseline = raw_mesh.copy()
baseline.data = raw_mesh.data.copy()
baseline.name = 'REF_CompleteRootWeights'
col.objects.link(baseline)
world = raw_mesh.matrix_world.copy()
baseline.parent = arm
baseline.matrix_world = world
for mod in baseline.modifiers:
    if mod.type == 'ARMATURE':
        mod.object = arm
root_group = baseline.vertex_groups.new(name='spine')
restored = 0
for vertex in baseline.data.vertices:
    missing = 1.0 - sum(g.weight for g in vertex.groups)
    if missing > 1e-6:
        root_group.add([vertex.index], missing, 'REPLACE')
        restored += 1
assert restored == 7070, restored
work = baseline.copy()
work.data = baseline.data.copy()
work.name = 'SK_YiSunSin_NPC'
col.objects.link(work)
baseline.hide_set(True)
baseline.hide_render = True
bpy.data.collections['01_Source_Reference'].hide_viewport = True
bpy.data.collections['01_Source_Reference'].hide_render = True
# Animation references must stay in the dependency graph while their root tracks
# are sampled. A disabled collection leaves evaluated transforms stale.
bpy.data.collections['02_Animation_Reference'].hide_viewport = False
bpy.data.collections['02_Animation_Reference'].hide_render = True

animations = {}
for name in ['Walk', 'Idle', 'Run', 'Jump']:
    if name == 'Walk':
        source = bpy.data.objects['spine.001']
    else:
        before = set(bpy.data.objects)
        bpy.ops.import_scene.fbx(filepath=str(ROOT / ('Reference/AS_YiSunSin_' + name + '.fbx')),
                                use_anim=True, automatic_bone_orientation=False,
                                ignore_leaf_bones=False)
        source = next(o for o in bpy.data.objects if o not in before and o.type == 'ARMATURE')
        for old in list(source.users_collection):
            old.objects.unlink(source)
        bpy.data.collections['02_Animation_Reference'].objects.link(source)
    source.hide_render = True
    source.hide_set(False)
    action = source.animation_data.action.copy()
    action.name = 'NPC_' + name
    action.use_fake_user = True
    channels = action.layers[0].strips[0].channelbag(action.slots[0]).fcurves
    for curve in list(channels):
        if not curve.data_path.startswith('pose.bones['):
            channels.remove(curve)
    arm.animation_data_create()
    arm.animation_data.action = action
    arm.animation_data.action_slot = action.slots[0]
    root_pose = arm.pose.bones['spine']
    root_pose.rotation_mode = 'QUATERNION'
    for bone in source.pose.bones:
        arm.pose.bones[bone.name].rotation_mode = bone.rotation_mode
    start, end = [int(v) for v in source.animation_data.action.frame_range]
    for frame in range(start, end + 1):
        bpy.context.scene.frame_set(frame)
        evaluated_source = source.evaluated_get(bpy.context.evaluated_depsgraph_get())
        root_pose.matrix_basis = arm.matrix_world.inverted() @ evaluated_source.matrix_world
        for prop in ['location', 'rotation_quaternion', 'scale']:
            root_pose.keyframe_insert(data_path=prop, frame=frame, group='spine')
    animations[name] = {'action': action.name, 'start': start, 'end': end, 'source': source.name}

arm.animation_data.action = bpy.data.actions['NPC_Walk']
arm.animation_data.action_slot = arm.animation_data.action.slots[0]
bpy.context.scene.frame_start = 1
bpy.context.scene.frame_end = animations['Walk']['end']
bpy.context.scene.frame_set(10)
colors = [(0.4, 0.045, 0.025, 1), (.62, .37, .22, 1), (.035, .025, .02, 1),
          (.78, .48, .04, 1), (.025, .065, .16, 1), (.32, .33, .33, 1),
          (.75, .73, .65, 1), (.035, .025, .02, 1), (.025, .025, .025, 1)]
for material, color in zip(work.data.materials, colors):
    material.diffuse_color = color
arm.hide_set(True)
for area in bpy.context.screen.areas:
    if area.type == 'VIEW_3D':
        area.spaces.active.overlay.show_overlays = False
        area.spaces.active.shading.color_type = 'MATERIAL'
        area.spaces.active.region_3d.view_location = Vector((0, 0, 1))
        area.spaces.active.region_3d.view_distance = 3.1

report = {'root_restored_vertices': restored, 'bones': len(arm.data.bones),
          'root': 'spine', 'animations': animations, 'skinning': 'linear blend',
          'source_mesh_untouched': True}
(ROOT / 'Audit/preparation.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
bpy.ops.wm.save_as_mainfile(filepath=str(ROOT / 'YiSunSin_NPC.blend'))
result = report
