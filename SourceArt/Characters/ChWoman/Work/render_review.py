import bpy,math,os,json
from mathutils import Vector,Quaternion
BASE=r'C:\Users\I\Documents\GitHub\ArtisticSW2026\SourceArt\Characters\ChWoman';os.makedirs(BASE+'/Review',exist_ok=True)
s=bpy.context.scene;r=bpy.data.objects['Woman_CTRL'];m=bpy.data.objects['SKM_ChWoman_UE5'];a=bpy.data.objects['Woman_EXPORT']
if not s.world:s.world=bpy.data.worlds.new('Woman_StudioWorld')
s.world.use_nodes=True;s.world.node_tree.nodes['Background'].inputs['Color'].default_value=(.12,.15,.2,1);s.world.node_tree.nodes['Background'].inputs['Strength'].default_value=.4
col=bpy.data.collections.get('04_REVIEW')
if not col:col=bpy.data.collections.new('04_REVIEW');s.collection.children.link(col)
if not s.camera:
    cam=bpy.data.cameras.new('Woman_ReviewCamera');obj=bpy.data.objects.new(cam.name,cam);col.objects.link(obj);s.camera=obj;cam.type='ORTHO';cam.ortho_scale=1.95
    for name,location,energy,size in [('Key',(2,-3,4),650,4),('Fill',(-3,-1,2),400,3),('Rim',(0,3,3),700,3)]:
        light=bpy.data.lights.new('Woman_'+name,'AREA');light.energy=energy;light.shape='DISK';light.size=size;o=bpy.data.objects.new(light.name,light);col.objects.link(o);o.location=location;o.rotation_euler=(Vector((0,0,.9))-o.location).to_track_quat('-Z','Y').to_euler()
s.render.engine='CYCLES';s.cycles.samples=24;s.cycles.use_denoising=True;s.render.resolution_x=768;s.render.resolution_y=1024;s.render.resolution_percentage=100;s.render.image_settings.file_format='PNG';s.render.film_transparent=False
s.camera.location=(2.1,-4,1.75);target=Vector((0,0,.83));s.camera.rotation_euler=(target-s.camera.location).to_track_quat('-Z','Y').to_euler()
for p in r.pose.bones:p.matrix_basis.identity()
for limb in ['arm_l','arm_r','leg_l','leg_r']:r.pose.bones['FK_root']['ik_'+limb]=0.0
r.update_tag();bpy.context.view_layer.update()
s.render.filepath=BASE+'/Review/01_Quinn_Apose.png';bpy.ops.render.render(write_still=True)
for side,sign in [('l',-1),('r',1)]:
    p=r.pose.bones['FK_upperarm_'+side];p.rotation_mode='QUATERNION';axis=p.bone.matrix_local.to_quaternion().inverted()@Vector((0,1,0));p.rotation_quaternion=Quaternion(axis,math.radians(sign*75))
r.update_tag();bpy.context.view_layer.update();s.render.filepath=BASE+'/Review/02_Arms_Raised.png';bpy.ops.render.render(write_still=True)
for p in r.pose.bones:p.matrix_basis.identity();p.rotation_mode='XYZ'
r.update_tag();bpy.context.view_layer.update()
bpy.ops.wm.save_as_mainfile(filepath=BASE+'/ChWoman_UE5_Rig.blend')
result={'renders':['01_Quinn_Apose.png','02_Arms_Raised.png']}
