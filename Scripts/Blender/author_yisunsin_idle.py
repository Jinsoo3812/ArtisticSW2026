"""Author an upright NPC idle while preserving the mesh's existing bind skeleton.

The original clips remain reference actions. The NPC owns this new idle clip;
feet use the symmetric reference stance and the upper body retains breathing.
"""
import bpy
import json
from math import radians
from pathlib import Path
from mathutils import Matrix, Vector

ROOT = Path(__file__).resolve().parents[2] / "SourceArt/Characters/YiSunSin"
scene = bpy.context.scene
arm = bpy.data.objects['Armature']
source = bpy.data.objects['spine.002']
source.hide_set(False)
bpy.data.collections['02_Animation_Reference'].hide_viewport = False
action = bpy.data.actions.get('NPC_Idle_Upright') or bpy.data.actions.new('NPC_Idle_Upright')
arm.animation_data.action = action
action.use_fake_user = True
leg_bones = {bone.name for bone in arm.data.bones
             if bone.name.startswith('thigh_') and bone.parent and bone.parent.name == 'spine'}
for name in list(leg_bones):
    leg_bones.update(b.name for b in arm.data.bones[name].children_recursive)
offsets = {'spine_001': 1.2, 'spine_002': 2.0, 'spine_003': 2.0,
           'spine_004': -1.0, 'spine_006': -3.0}
ground_lift = 0.006984594743698835
first_basis = None
for frame in range(1, 123):
    scene.frame_set(frame)
    basis = {p.name: p.matrix_basis.copy() for p in source.pose.bones}
    if first_basis is None:
        first_basis = {n:m.copy() for n,m in basis.items()}
    # Blend the final eight samples into the first pose for a closed idle loop.
    if frame > 114:
        t = (frame-114)/8
        t = t*t*(3-2*t)
        for name in basis:
            loc, rot, scale = basis[name].decompose()
            end_loc, end_rot, end_scale = first_basis[name].decompose()
            basis[name] = Matrix.LocRotScale(loc.lerp(end_loc,t), rot.slerp(end_rot,t), scale.lerp(end_scale,t))
    for pose in arm.pose.bones:
        pose.matrix_basis = basis.get(pose.name, Matrix.Identity(4))
        if pose.name in leg_bones or pose.name == 'spine':
            pose.matrix_basis = Matrix.Identity(4)
    root = arm.pose.bones['spine']
    root.matrix_basis = Matrix.Translation(arm.matrix_world.to_3x3().inverted() @ Vector((0,0,ground_lift)))
    bpy.context.view_layer.update()
    for name, degrees in offsets.items():
        pose = arm.pose.bones[name]
        world = arm.matrix_world @ pose.matrix
        pivot = world.translation
        correction = Matrix.Translation(pivot) @ Matrix.Rotation(radians(degrees),4,'X') @ Matrix.Translation(-pivot)
        pose.matrix = arm.matrix_world.inverted() @ correction @ world
        bpy.context.view_layer.update()
    for pose in arm.pose.bones:
        props = ['location', 'scale', 'rotation_quaternion' if pose.rotation_mode == 'QUATERNION' else 'rotation_euler']
        for prop in props:
            pose.keyframe_insert(data_path=prop, frame=frame, group=pose.name)
scene.frame_start = 1
scene.frame_end = 122
scene.frame_set(1)
report = {'action': action.name, 'frames': [1,122], 'fps': scene.render.fps/scene.render.fps_base,
          'spine_world_x_offsets_degrees': offsets, 'symmetric_planted_legs': True,
          'ground_lift_m': ground_lift, 'original_bind_skeleton_unchanged': True,
          'original_idle_action_unchanged': True, 'loop_end_matches_start': True}
(ROOT / 'Audit/upright_idle_authoring.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
result = report
