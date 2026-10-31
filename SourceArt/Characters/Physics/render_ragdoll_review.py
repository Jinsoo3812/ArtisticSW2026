"""Render native UE ragdoll poses on isolated Blender copies; never save source rigs."""
import bpy,json,math
from pathlib import Path
from mathutils import Matrix,Quaternion,Vector
ROOT=Path(r'C:\Users\I\Documents\GitHub\ArtisticSW2026')
report=json.loads((ROOT/'Saved/PlayerRagdollValidation/pie_full.json').read_text())
reflection=Matrix.Diagonal((1,-1,1))
def rotation(d):
    x,y,z,w=d['rotation'];return reflection@Quaternion((w,x,y,z)).to_matrix()@reflection
def position(d):return reflection@Vector(d['position'])*.01
original=bpy.context.window.scene
scene=bpy.data.scenes.new('Ragdoll_Review');bpy.context.window.scene=scene
scene.render.engine='CYCLES';scene.cycles.samples=24
scene.render.resolution_x=900;scene.render.resolution_y=700;scene.render.resolution_percentage=100
scene.world=bpy.data.worlds.new('Ragdoll_Review_World');scene.world.color=(.22,.22,.22)
camera=bpy.data.objects.new('Review_Camera',bpy.data.cameras.new('Review_Camera'));scene.collection.objects.link(camera);scene.camera=camera;camera.data.type='ORTHO'
lights=[]
for name,loc,power,size in [('Key',(1,-3,6),1600,5),('Fill',(-4,1,4),1200,4)]:
    data=bpy.data.lights.new(name,'AREA');data.energy=power;data.shape='DISK';data.size=size
    light=bpy.data.objects.new(name,data);scene.collection.objects.link(light);light.location=loc
    light.rotation_euler=(-light.location).to_track_quat('-Z','Y').to_euler()
    lights.append((light,Vector(loc)))
bpy.ops.mesh.primitive_plane_add(size=200,location=(0,0,0));floor=bpy.context.object
material=bpy.data.materials.new('Review_Floor');material.diffuse_color=(.16,.19,.22,1);floor.data.materials.append(material)
images=[];bounds=[]
for label,arm_name in [('Man','Armature'),('Woman','Woman_EXPORT')]:
    source_arm=bpy.data.objects[arm_name];source_mesh=bpy.data.objects['SKM_Ch'+label+'_UE5']
    arm=source_arm.copy();arm.data=source_arm.data.copy();arm.animation_data_clear();scene.collection.objects.link(arm);arm.hide_render=False;arm.hide_set(False)
    for p in arm.pose.bones:
        for c in list(p.constraints):p.constraints.remove(c)
    mesh=source_mesh.copy();mesh.data=source_mesh.data.copy();mesh.parent=None;mesh.animation_data_clear();scene.collection.objects.link(mesh);mesh.hide_render=False;mesh.hide_set(False)
    for modifier in mesh.modifiers:
        if modifier.type=='ARMATURE':modifier.object=arm
    reference=json.loads((ROOT/('Saved/Ch'+label+'RigValidation/candidates.json')).read_text())
    ref={b['name']:b for b in reference['prepared_new_mesh']['bones']}
    offsets={n:rotation(ref[n]).inverted()@b.matrix_local.to_3x3().normalized() for n,b in arm.data.bones.items() if n in ref}
    material_vertices={material.name:set() for material in mesh.data.materials}
    for polygon in mesh.data.polygons:material_vertices[mesh.data.materials[polygon.material_index].name].update(polygon.vertices)
    for mode in ['idle','jump','platform']:
        case=next(c for c in report['cases'] if c['character']==label and c['mode']==mode and c['requested_fps']==60)
        sample=case['dead_pose'];bones={b['name']:b for b in sample['bones']};world=sample['mesh_world']
        transform=rotation(world).to_4x4()@Matrix.Diagonal((*world['scale'],1));transform.translation=position(world)-Vector((0,0,10))
        arm.matrix_world=transform;mesh.matrix_world=transform
        for p in arm.pose.bones:p.matrix_basis.identity()
        for p in sorted(arm.pose.bones,key=lambda p:len(p.parent_recursive)):
            if p.name in offsets and p.name in bones:
                matrix=(rotation(bones[p.name])@offsets[p.name]).to_4x4();matrix.translation=position(bones[p.name]);p.matrix=matrix;bpy.context.view_layer.update()
        evaluated=mesh.evaluated_get(bpy.context.evaluated_depsgraph_get());coords=[mesh.matrix_world@v.co for v in evaluated.data.vertices]
        low=Vector([min(v[i] for v in coords) for i in range(3)]);high=Vector([max(v[i] for v in coords) for i in range(3)]);target=(low+high)*.5
        for light,offset in lights:
            light.location=target+offset;light.rotation_euler=(-offset).to_track_quat('-Z','Y').to_euler()
        camera.location=target+Vector((2,-4,4));camera.rotation_euler=(target-camera.location).to_track_quat('-Z','Y').to_euler();camera.data.ortho_scale=max(2.5,(high-low).length*1.1)
        path=ROOT/('SourceArt/Characters/Physics/Review/'+label+'_'+mode+'.png');path.parent.mkdir(exist_ok=True)
        scene.render.filepath=str(path)
        if not globals().get('RAGDOLL_SKIP_RENDER',False):bpy.ops.render.render(write_still=True)
        images.append(str(path));bounds.append({'character':label,'mode':mode,'lowest_vertex_cm':low.z*100,'material_minimum_cm':{name:min(coords[i].z for i in indices)*100 for name,indices in material_vertices.items() if indices}})
    mesh.hide_render=True;arm.hide_render=True
bpy.context.window.scene=original
result={'images':images,'bounds':bounds,'note':'Native UE evaluated component-space poses, reconstructed on Blender copies; floor is actual UE support plane, source files not saved.'}
(ROOT/'Saved/PlayerRagdollValidation/render_review.json').write_text(json.dumps(result,indent=2),encoding='utf8')
