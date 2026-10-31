"""Export the current aligned rig through the tested centimetre recipe."""
import bpy
from mathutils import Matrix
BASE=r'C:\Users\I\Documents\GitHub\ArtisticSW2026\SourceArt\Characters\ChMan'
a=bpy.data.objects['Armature'];m=bpy.data.objects['SKM_ChMan_UE5']
assert all(p.matrix_basis.is_identity for p in a.pose.bones)
bpy.ops.object.select_all(action='DESELECT');a.select_set(True);m.select_set(True);bpy.context.view_layer.objects.active=a
unit=bpy.context.scene.unit_settings.scale_length
try:
    m.data.transform(Matrix.Scale(100,4));a.data.transform(Matrix.Scale(100,4))
    bpy.context.scene.unit_settings.scale_length=.01;bpy.context.view_layer.update()
    bpy.ops.export_scene.fbx(filepath=BASE+'\\FBX\\SKM_ChMan_UE5.fbx',use_selection=True,object_types={'ARMATURE','MESH'},axis_forward='-Y',axis_up='Z',apply_unit_scale=True,apply_scale_options='FBX_SCALE_NONE',add_leaf_bones=False,use_armature_deform_only=False,bake_anim=False,mesh_smooth_type='FACE',use_tspace=True,path_mode='STRIP',armature_nodetype='NULL',primary_bone_axis='Y',secondary_bone_axis='X')
finally:
    m.data.transform(Matrix.Scale(.01,4));a.data.transform(Matrix.Scale(.01,4));bpy.context.scene.unit_settings.scale_length=unit;bpy.context.view_layer.update()
bpy.ops.wm.save_as_mainfile(filepath=BASE+'\\ChMan_UE5_Rig.blend')
result={'exported':BASE+'\\FBX\\SKM_ChMan_UE5.fbx'}
