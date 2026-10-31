"""Export bind, or an explicitly requested baked range, excluding authoring rig.

Run from this blend. Set EXPORT_ANIMATION=True, FRAME_START/END for animation.
The evaluated copy is baked; source controllers/Actions are never flattened.
"""
import bpy,os,json
from mathutils import Matrix
BASE=r'C:\Users\I\Documents\GitHub\ArtisticSW2026\SourceArt\Characters\ChWoman'
original=bpy.data.objects['Woman_EXPORT'];mesh=bpy.data.objects['SKM_ChWoman_UE5'];animation=globals().get('EXPORT_ANIMATION',False)
was_pose=bpy.context.object is not None and bpy.context.object.mode=='POSE'
if bpy.context.object and bpy.context.object.mode!='OBJECT':bpy.ops.object.mode_set(mode='OBJECT')
os.makedirs(BASE+'/FBX',exist_ok=True)
a=original.copy();a.data=original.data.copy();bpy.context.scene.collection.objects.link(a);a.name='Armature';a.hide_set(False);a.hide_render=True
m=mesh.copy();m.data=mesh.data.copy();bpy.context.scene.collection.objects.link(m);m.name='WomanExportMesh';m.parent=a;m.matrix_parent_inverse=Matrix.Identity(4);m.matrix_world=Matrix.Identity(4)
for mod in m.modifiers:
    if mod.type=='ARMATURE':mod.object=a
unit=bpy.context.scene.unit_settings.scale_length;frame=bpy.context.scene.frame_current;frame_range=(bpy.context.scene.frame_start,bpy.context.scene.frame_end)
try:
    bpy.ops.object.select_all(action='DESELECT');a.select_set(True);bpy.context.view_layer.objects.active=a
    if animation:
        bpy.context.scene.frame_start=int(globals().get('FRAME_START',1));bpy.context.scene.frame_end=int(globals().get('FRAME_END',30))
        bpy.ops.object.mode_set(mode='POSE');bpy.ops.pose.select_all(action='SELECT')
        bpy.ops.nla.bake(frame_start=int(globals().get('FRAME_START',1)),frame_end=int(globals().get('FRAME_END',30)),step=1,only_selected=True,visual_keying=True,clear_constraints=True,clear_parents=False,use_current_action=False,bake_types={'POSE'})
        bpy.ops.object.mode_set(mode='OBJECT')
        for driver in list(a.animation_data.drivers):a.animation_data.drivers.remove(driver)
    else:
        a.animation_data_clear()
        for p in a.pose.bones:
            for c in list(p.constraints):p.constraints.remove(c)
            p.matrix_basis.identity()
    m.data.transform(Matrix.Scale(100,4));a.data.transform(Matrix.Scale(100,4));bpy.context.scene.unit_settings.scale_length=.01
    if animation:
        # Scale evaluated local translations in the new baked Action only.
        action=a.animation_data.action
        for layer in action.layers:
            for strip in layer.strips:
                for bag in strip.channelbags:
                    for curve in bag.fcurves:
                        if curve.data_path.endswith('.location'):
                            for k in curve.keyframe_points:k.co[1]*=100;k.handle_left[1]*=100;k.handle_right[1]*=100
    bpy.context.view_layer.update();m.select_set(True)
    filename=globals().get('EXPORT_OUTPUT_PATH',BASE+'/FBX/'+('AS_ChWoman_Baked.fbx' if animation else 'SKM_ChWoman_UE5.fbx'))
    os.makedirs(os.path.dirname(os.path.abspath(filename)),exist_ok=True)
    bpy.ops.export_scene.fbx(filepath=filename,use_selection=True,object_types={'ARMATURE','MESH'},axis_forward='-Y',axis_up='Z',apply_unit_scale=True,apply_scale_options='FBX_SCALE_NONE',add_leaf_bones=False,use_armature_deform_only=False,bake_anim=animation,bake_anim_use_all_actions=False,bake_anim_use_nla_strips=False,bake_anim_simplify_factor=0,mesh_smooth_type='FACE',use_tspace=True,path_mode='STRIP',armature_nodetype='NULL',primary_bone_axis='Y',secondary_bone_axis='X')
    if animation:
        with open(os.path.splitext(filename)[0]+'.export.json','w',encoding='utf8') as stream:
            json.dump({'frame_start':bpy.context.scene.frame_start,'frame_end':bpy.context.scene.frame_end,'fps':bpy.context.scene.render.fps,'fps_base':bpy.context.scene.render.fps_base,'bones':len(a.data.bones)},stream,indent=2)
finally:
    if bpy.context.object and bpy.context.object.mode!='OBJECT':bpy.ops.object.mode_set(mode='OBJECT')
    bpy.context.scene.unit_settings.scale_length=unit
    bpy.context.scene.frame_start,bpy.context.scene.frame_end=frame_range
    bpy.data.objects.remove(m,do_unlink=True);bpy.data.objects.remove(a,do_unlink=True);bpy.context.scene.frame_set(frame)
    bpy.ops.object.select_all(action='DESELECT');r=bpy.data.objects['Woman_CTRL'];r.select_set(True);bpy.context.view_layer.objects.active=r;bpy.context.view_layer.update()
    if was_pose:bpy.ops.object.mode_set(mode='POSE')
result={'exported':filename,'animation':animation,'bones':len(original.data.bones)}
