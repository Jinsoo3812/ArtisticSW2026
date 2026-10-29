"""Export an unchanged control and the NPC mesh with identical FBX settings."""
import bpy
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2] / "SourceArt/Characters/YiSunSin"
out = ROOT / 'FBX'
out.mkdir(exist_ok=True)
arm = bpy.data.objects['Armature']
assert len(arm.data.bones)==83, 'Use export_yisunsin_sash.py for the 87-bone NPC; preserve the original 83-bone control export'
saved_pose = arm.data.pose_position
selected = list(bpy.context.selected_objects)
active = bpy.context.view_layer.objects.active
hidden = {o: o.hide_get() for o in bpy.data.collections['03_NPC_Working'].objects}
paths = []
try:
    arm.data.pose_position = 'REST'
    for name, filename in [('REF_CompleteRootWeights', 'SK_YiSunSin_UnchangedControl.fbx'),
                           ('SK_YiSunSin_NPC', 'SK_YiSunSin_NPC.fbx')]:
        mesh = bpy.data.objects[name]
        bpy.ops.object.select_all(action='DESELECT')
        for obj in [arm, mesh]:
            obj.hide_set(False)
            obj.select_set(True)
        bpy.context.view_layer.objects.active = arm
        path = out / filename
        status = bpy.ops.export_scene.fbx(
            filepath=str(path), check_existing=False, use_selection=True,
            object_types={'ARMATURE', 'MESH'}, apply_unit_scale=True,
            apply_scale_options='FBX_SCALE_NONE', axis_forward='-Y', axis_up='Z',
            add_leaf_bones=False, use_armature_deform_only=False,
            armature_nodetype='NULL', primary_bone_axis='Y', secondary_bone_axis='X',
            bake_anim=False, path_mode='STRIP', use_mesh_modifiers=True,
            use_tspace=True, mesh_smooth_type='FACE')
        assert status == {'FINISHED'}, status
        paths.append({'file': filename, 'bytes': path.stat().st_size})
finally:
    arm.data.pose_position = saved_pose
    bpy.ops.object.select_all(action='DESELECT')
    for obj, hide in hidden.items():
        obj.hide_set(hide)
    for obj in selected:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = active
idle = bpy.data.actions.get('NPC_Idle_Upright')
if idle:
    prior_action = arm.animation_data.action
    prior_range = (bpy.context.scene.frame_start, bpy.context.scene.frame_end)
    try:
        arm.animation_data.action = idle
        arm.animation_data.action_slot = idle.slots[0]
        bpy.context.scene.frame_start, bpy.context.scene.frame_end = 1, 122
        bpy.ops.object.select_all(action='DESELECT')
        arm.hide_set(False)
        arm.select_set(True)
        bpy.context.view_layer.objects.active = arm
        path = out / 'AS_YiSunSin_NPC_Idle_Upright.fbx'
        status = bpy.ops.export_scene.fbx(
            filepath=str(path), check_existing=False, use_selection=True,
            object_types={'ARMATURE'}, apply_unit_scale=True,
            apply_scale_options='FBX_SCALE_NONE', axis_forward='-Y', axis_up='Z',
            add_leaf_bones=False, use_armature_deform_only=False,
            armature_nodetype='NULL', primary_bone_axis='Y', secondary_bone_axis='X',
            bake_anim=True, bake_anim_use_all_bones=True, bake_anim_use_all_actions=False,
            bake_anim_use_nla_strips=False, bake_anim_simplify_factor=0.0)
        assert status == {'FINISHED'}
        paths.append({'file': path.name, 'bytes': path.stat().st_size})
    finally:
        arm.animation_data.action = prior_action
        arm.animation_data.action_slot = prior_action.slots[0] if prior_action else None
        bpy.context.scene.frame_start, bpy.context.scene.frame_end = prior_range
        arm.hide_set(hidden[arm])
report = {'files': paths, 'bones_including_root': len(arm.data.bones),
          'root': 'spine', 'leaf_bones': False, 'all_bones': True,
          'npc_idle_exported': bool(idle), 'scene_unit_m': bpy.context.scene.unit_settings.scale_length}
(ROOT / 'Audit/fbx_export.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
bpy.ops.wm.save_as_mainfile(filepath=str(ROOT / 'YiSunSin_NPC.blend'))
result = report
