import bpy,json
from mathutils import Vector,Matrix,Quaternion
BASE=r'C:\Users\I\Documents\GitHub\ArtisticSW2026\SourceArt\Characters\ChWoman'
SAVED=r'C:\Users\I\Documents\GitHub\ArtisticSW2026\Saved\ChWomanRigValidation'
report=json.load(open(SAVED+'\\final_saved_connection.json',encoding='utf8'))
info=json.load(open(SAVED+'\\candidates.json',encoding='utf8'))
ref={b['name']:b for b in info['prepared_new_mesh']['bones']}
a=bpy.data.objects['Woman_EXPORT'];m=bpy.data.objects['SKM_ChWoman_UE5'];scene=bpy.context.scene
reflection=Matrix.Diagonal((1,-1,1))
def rot(d):
    x,y,z,w=d['rotation'];return reflection@Quaternion((w,x,y,z)).to_matrix()@reflection
def pos(d):return reflection@Vector(d['position'])*.01
offsets={n:rot(ref[n]).inverted()@b.matrix_local.to_3x3().normalized() for n,b in a.data.bones.items() if n in ref}
constraints=[(c,c.mute) for p in a.pose.bones for c in p.constraints]
for c,_ in constraints:c.mute=True
images=[]
for case in ['saved_woman_60']:
    for phase in ['idle','run','jump_forward']:
        sample=next(v for v in report['phase_snapshots'] if v['case']==case and v['phase']==phase)
        bones={b['name']:b for b in sample['snapshot']['bones']}
        for p in a.pose.bones:p.matrix_basis.identity()
        for p in sorted(a.pose.bones,key=lambda p:len(p.parent_recursive)):
            if p.name not in offsets or p.name not in bones:continue
            d=bones[p.name];matrix=(rot(d)@offsets[p.name]).to_4x4();matrix.translation=pos(d);p.matrix=matrix;bpy.context.view_layer.update()
        evaluated=m.evaluated_get(bpy.context.evaluated_depsgraph_get());coords=[v.co for v in evaluated.data.vertices]
        low=Vector(tuple(min(v[i] for v in coords) for i in range(3)));high=Vector(tuple(max(v[i] for v in coords) for i in range(3)));target=(low+high)*.5
        scene.camera.location=target+Vector((2.5,-5,1.25));scene.camera.rotation_euler=(target-scene.camera.location).to_track_quat('-Z','Y').to_euler();scene.camera.data.ortho_scale=max(2.2,(high-low).z*1.15)
        path=BASE+'\\Review\\PIE_'+case+'_'+phase+'.png';scene.render.filepath=path;bpy.ops.render.render(write_still=True);images.append(path)
for p in a.pose.bones:p.matrix_basis.identity()
for c,mute in constraints:c.mute=mute
bpy.context.view_layer.update()
result={'images':images}
