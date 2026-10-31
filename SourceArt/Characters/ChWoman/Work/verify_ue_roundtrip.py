import bpy,json,math
from mathutils.kdtree import KDTree
BASE=r'C:\Users\I\Documents\GitHub\ArtisticSW2026\SourceArt\Characters\ChWoman'
before=set(bpy.data.objects)
bpy.ops.import_scene.fbx(filepath=BASE+'\\Reference\\Woman_UE_RoundTrip.fbx',automatic_bone_orientation=False)
objects=[o for o in bpy.data.objects if o not in before]
for o in objects:o.hide_render=True;o.hide_set(True)
rt=next(o for o in objects if o.type=='ARMATURE');rt.name='UE_Verified_Rig'
rm=next(o for o in objects if o.type=='MESH');rm.name='UE_Verified_Mesh'
a=bpy.data.objects['Woman_EXPORT'];m=bpy.data.objects['SKM_ChWoman_UE5']
kd=KDTree(len(rm.data.vertices))
for v in rm.data.vertices:kd.insert(rm.matrix_world@v.co,v.index)
kd.balance()
distances=[kd.find(m.matrix_world@v.co)[2]*100 for v in m.data.vertices]
common=set(a.data.bones.keys())&set(rt.data.bones.keys())
head_delta=max(((a.matrix_world@a.data.bones[n].head_local)-(rt.matrix_world@rt.data.bones[n].head_local)).length*100 for n in common)
def angle(q1,q2):
    q1.normalize();q2.normalize()
    return math.degrees(2*math.acos(min(1,abs(q1.dot(q2)))))
angles={n:angle(a.matrix_world.to_quaternion()@a.data.bones[n].matrix_local.to_quaternion(),rt.matrix_world.to_quaternion()@rt.data.bones[n].matrix_local.to_quaternion()) for n in common}
report={'blender_bones':len(a.data.bones),'ue_fbx_blender_bones':len(rt.data.bones),'root_note':'UE FBX represents root as the armature object, not a missing root.',
        'missing_except_object_root':sorted(set(a.data.bones.keys())-set(rt.data.bones.keys())-{'root'}),
        'max_vertex_nearest_delta_cm':max(distances),'max_original_surface_delta_cm':max(distances[:54393]),
        'max_bone_head_delta_cm':head_delta,'max_axis_angle_deg':max(angles.values()),
        'mesh_height_cm':rm.dimensions.z*100,'root_object_scale':list(rt.scale)}
with open(BASE+'\\Work\\ue_roundtrip_audit.json','w') as f:json.dump(report,f,indent=2)
result=report
