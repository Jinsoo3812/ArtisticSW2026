"""Export only the NPC sash variant, keeping pre-sash controls and FBX intact."""
import bpy,json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]/'SourceArt/Characters/YiSunSin'
arm=bpy.data.objects['Armature'];mesh=bpy.data.objects['SK_YiSunSin_NPC'];scene=bpy.context.scene
settings=dict(check_existing=False,use_selection=True,apply_unit_scale=True,apply_scale_options='FBX_SCALE_NONE',
              axis_forward='-Y',axis_up='Z',add_leaf_bones=False,use_armature_deform_only=False,
              armature_nodetype='NULL',primary_bone_axis='Y',secondary_bone_axis='X',path_mode='STRIP')
old_action=arm.animation_data.action;old_pose=arm.data.pose_position;old_range=(scene.frame_start,scene.frame_end)
files=[]
try:
    bpy.ops.object.select_all(action='DESELECT')
    for obj in [arm,mesh]:obj.hide_set(False);obj.select_set(True)
    bpy.context.view_layer.objects.active=arm;arm.data.pose_position='REST'
    filename='SK_YiSunSin_NPC_Sash.fbx'
    assert bpy.ops.export_scene.fbx(filepath=str(ROOT/'FBX'/filename),object_types={'ARMATURE','MESH'},
        bake_anim=False,use_tspace=True,mesh_smooth_type='FACE',**settings)=={'FINISHED'}
    files.append(filename);arm.data.pose_position='POSE';mesh.select_set(False)
    for label,end in [('Idle_Upright',122),('Walk',36),('Run',26)]:
        action=bpy.data.actions['NPC_Sash_'+label]
        arm.animation_data.action=action;arm.animation_data.action_slot=action.slots[0]
        scene.frame_start=1;scene.frame_end=end;scene.frame_set(1)
        filename='AS_YiSunSin_NPC_Sash_'+label+'.fbx'
        assert bpy.ops.export_scene.fbx(filepath=str(ROOT/'FBX'/filename),object_types={'ARMATURE'},
            bake_anim=True,bake_anim_use_all_bones=True,bake_anim_use_all_actions=False,
            bake_anim_use_nla_strips=False,bake_anim_simplify_factor=0.0,**settings)=={'FINISHED'}
        files.append(filename)
finally:
    arm.animation_data.action=old_action;arm.animation_data.action_slot=old_action.slots[0]
    arm.data.pose_position=old_pose;scene.frame_start,scene.frame_end=old_range;scene.frame_set(1)
result={'files':files,'bones':len(arm.data.bones)}
(ROOT/'Audit/sash_export.json').write_text(json.dumps(result,indent=2))
bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/'YiSunSin_NPC.blend'))
