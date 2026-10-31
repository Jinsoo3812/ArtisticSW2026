"""Render native UE-evaluated mesh poses, not a guessed animation mapping."""
import bpy,json,math
from mathutils import Vector,Matrix,Quaternion
BASE=r'C:\Users\I\Documents\GitHub\ArtisticSW2026\SourceArt\Characters\ChMan'
report=json.load(open(BASE+'\\Work\\ue_pose_samples.json',encoding='utf8'))
a=bpy.data.objects['Armature'];s=bpy.context.scene
reference=report['reference_bones']['ChMan']
reflection=Matrix.Diagonal((1,-1,1))
def rotation(d):
    x,y,z,w=d['rotation']
    return reflection@Quaternion((w,x,y,z)).to_matrix()@reflection
def position(d):return reflection@Vector(d['translation'])*.01
offsets={n:rotation(reference[n]).inverted()@b.matrix_local.to_3x3().normalized() for n,b in a.data.bones.items() if n in reference}
validation={'reference_head_max_delta_cm':max((a.data.bones[n].head_local-position(reference[n])).length*100 for n in offsets),'limb_length_samples':[]}
muted=[(c,c.mute) for pb in a.pose.bones for c in pb.constraints]
for c,_ in muted:c.mute=True
for sample in report['samples']:
    if sample['body']!='ChMan':continue
    bones=sample['bones']
    lengths={}
    for start,end in [('upperarm_l','lowerarm_l'),('lowerarm_l','hand_l'),('thigh_l','calf_l'),('calf_l','foot_l')]:
        expected=(position(reference[start])-position(reference[end])).length*100
        actual=(position(bones[start])-position(bones[end])).length*100
        lengths[start]={'bind_cm':expected,'posed_cm':actual,'delta_cm':abs(expected-actual)}
    validation['limb_length_samples'].append({'clip':sample['clip'],'time':sample['time'],'lengths':lengths})
for clip in ['Idle','Walk','Jump']:
    samples=[q for q in report['samples'] if q['body']=='ChMan' and q['clip']==clip]
    sample=samples[len(samples)//2]
    for pb in a.pose.bones:pb.matrix_basis.identity()
    ordered=sorted(a.pose.bones,key=lambda pb:len(pb.parent_recursive))
    for pb in ordered:
        n=pb.name
        if n not in offsets or n not in sample['bones']:continue
        d=sample['bones'][n]
        matrix=(rotation(d)@offsets[n]).to_4x4()
        matrix.translation=position(d)
        pb.matrix=matrix
        bpy.context.view_layer.update()
    bpy.context.view_layer.update()
    evaluated=bpy.data.objects['SKM_ChMan_UE5'].evaluated_get(bpy.context.evaluated_depsgraph_get())
    coords=[evaluated.matrix_world@v.co for v in evaluated.data.vertices]
    low=Vector(tuple(min(v[i] for v in coords) for i in range(3)))
    high=Vector(tuple(max(v[i] for v in coords) for i in range(3)))
    target=(low+high)*.5
    s.camera.location=target+Vector((2.5,-5,1.25));s.camera.rotation_euler=(target-s.camera.location).to_track_quat('-Z','Y').to_euler();s.camera.data.ortho_scale=max(2.2,(high-low).z*1.15)
    s.render.filepath=BASE+'\\Review\\UE_'+clip+'.png'
    bpy.ops.render.render(write_still=True)
for pb in a.pose.bones:pb.matrix_basis.identity()
for c,mute in muted:c.mute=mute
bpy.context.view_layer.update()
validation['max_limb_length_delta_cm']=max(v['delta_cm'] for q in validation['limb_length_samples'] for v in q['lengths'].values())
with open(BASE+'\\Work\\native_pose_validation.json','w') as f:json.dump(validation,f,indent=2)
result={'reference_head_max_delta_cm':validation['reference_head_max_delta_cm'],'max_limb_length_delta_cm':validation['max_limb_length_delta_cm'],'samples':len(validation['limb_length_samples'])}
