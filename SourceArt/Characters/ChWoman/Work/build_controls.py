"""Separate FK / non-stretch two-bone IK authoring rig. Never exported."""
import bpy,math,json
from mathutils import Vector,Matrix
BASE=r'C:\Users\I\Documents\GitHub\ArtisticSW2026\SourceArt\Characters\ChWoman'
a=bpy.data.objects['Woman_EXPORT'];assert not bpy.data.objects.get('Woman_CTRL')
collection=bpy.data.collections.new('02_CONTROLS');bpy.context.scene.collection.children.link(collection)
data=bpy.data.armatures.new('Woman_ControlRig');rig=bpy.data.objects.new('Woman_CTRL',data);collection.objects.link(rig)
rig.show_in_front=True;rig.display_type='WIRE';data.display_type='STICK'
bpy.ops.object.select_all(action='DESELECT');rig.select_set(True);bpy.context.view_layer.objects.active=rig;bpy.ops.object.mode_set(mode='EDIT')
eb=data.edit_bones
for bone in a.data.bones:
    b=eb.new('FK_'+bone.name);b.length=max(bone.length,.025);b.matrix=bone.matrix_local.copy();b.use_deform=False
for bone in a.data.bones:
    eb['FK_'+bone.name].parent=eb['FK_'+bone.parent.name] if bone.parent else None
limbs={}
for side in ['l','r']:
    for kind,upper,lower,end,anchor in [('arm','upperarm','lowerarm','hand','clavicle'),('leg','thigh','calf','foot','pelvis')]:
        names=[upper+'_'+side,lower+'_'+side,end+'_'+side];anchor=anchor+'_'+side if kind=='arm' else anchor
        key=kind+'_'+side;p0,p1,p2=[a.data.bones[n].head_local.copy() for n in names]
        chain=(p2-p0).normalized();bend=p1-(p0+chain*(p1-p0).dot(chain))
        if bend.length<.001:bend=Vector((0,-1,0))
        pole=p1+bend.normalized()*((p1-p0).length+(p2-p1).length)*.6
        target=eb.new('IK_'+key);target.length=.08;target.matrix=a.data.bones[names[2]].matrix_local.copy();target.parent=eb['FK_root'];target.use_deform=False
        pb=eb.new('POLE_'+key);pb.head=pole;pb.tail=pole+Vector((0,0,.05));pb.parent=eb['FK_root'];pb.use_deform=False
        prev=eb['FK_'+anchor]
        for i,n in enumerate(names):
            mech=eb.new('MCH_'+key+'_'+str(i));mech.parent=prev;mech.use_deform=False
            if i<2:mech.head=[p0,p1][i];mech.tail=[p1,p2][i]
            else:mech.length=.05;mech.matrix=a.data.bones[n].matrix_local.copy()
            mech.use_connect=i>0;prev=mech
            res=eb.new('RES_'+n);res.length=.035;res.matrix=a.data.bones[n].matrix_local.copy();res.parent=mech;res.use_deform=False
        limbs[key]={'names':names,'pole':list(pole),'mid':list(p1),'target':'IK_'+key}
bpy.ops.object.mode_set(mode='OBJECT')
groups={n:data.collections.new(n) for n in ['Root & Body','Arms FK','Legs FK','Hands & Fingers','Face','Extras','IK Goals','IK Poles','Mechanism']}
for b in data.bones:
    n=b.name
    if n.startswith(('MCH_','RES_')):group='Mechanism'
    elif n.startswith('IK_'):group='IK Goals'
    elif n.startswith('POLE_'):group='IK Poles'
    elif n.startswith('FK_ik_') or n in ['FK_interaction','FK_center_of_mass']:group='Mechanism'
    elif any(d in n for d in ['index','middle','ring_','pinky','thumb']):group='Hands & Fingers'
    elif any(d in n for d in ['jaw','eye','tongue','teeth','facial']):group='Face'
    elif n.startswith(('FK_upperarm','FK_lowerarm','FK_clavicle','FK_hand_')):group='Arms FK'
    elif n.startswith(('FK_thigh','FK_calf','FK_foot','FK_ball')):group='Legs FK'
    elif n.startswith(('FK_spine','FK_neck','FK_head','FK_pelvis','FK_root')):group='Root & Body'
    else:group='Extras'
    groups[group].assign(b);b.color.palette='THEME04' if n.endswith('_l') else ('THEME03' if n.endswith('_r') else 'THEME09')
groups['Mechanism'].is_visible=False;groups['Extras'].is_visible=False;groups['Face'].is_visible=False
widgets=bpy.data.collections.new('03_WIDGETS');bpy.context.scene.collection.children.link(widgets);widgets.hide_render=True
for name,verts,edges in [('Ring',[(math.cos(i*math.tau/24),math.sin(i*math.tau/24),0) for i in range(24)],[(i,(i+1)%24) for i in range(24)]),('Goal',[(-1,-1,0),(-1,1,0),(1,1,0),(1,-1,0)],[(0,1),(1,2),(2,3),(3,0)]),('Pole',[(0,0,1),(-1,0,0),(0,0,-1),(1,0,0)],[(0,1),(1,2),(2,3),(3,0)])]:
    mesh=bpy.data.meshes.new('WGT_'+name);mesh.from_pydata(verts,edges,[]);obj=bpy.data.objects.new('WGT_'+name,mesh);widgets.objects.link(obj);obj.hide_render=True;obj.hide_set(True)
for p in rig.pose.bones:
    p.rotation_mode='XYZ';p.lock_scale=(True,True,True)
    if p.name.startswith(('MCH_','RES_')):continue
    p.custom_shape=bpy.data.objects['WGT_'+('Goal' if p.name.startswith('IK_') else 'Pole' if p.name.startswith('POLE_') else 'Ring')]
    p.use_custom_shape_bone_size=False
    scale=.045 if p.name.startswith(('IK_','POLE_')) else .025
    if p.name=='FK_root':scale=.28
    elif p.name in ['FK_pelvis','FK_spine_03','FK_spine_05']:scale=.12
    elif any(x in p.name for x in ['index','middle','pinky','ring_','thumb']):scale=.012
    p.custom_shape_scale_xyz=(scale,scale,scale)
    if not p.name.startswith(('IK_','POLE_')) and p.name not in ['FK_root','FK_pelvis']:p.lock_location=(True,True,True)
root=rig.pose.bones['FK_root']
root['weapon_independent']=0.0;root.id_properties_ui('weapon_independent').update(min=0,max=1,description='0 follows solved wrist; 1 uses FK_ik_hand_gun independently. Match before changing.')
audit={'limbs':{},'controller_bones':len(data.bones),'export_bones':len(a.data.bones)}
for key,info in limbs.items():
    prop='ik_'+key;root[prop]=0.0;root.id_properties_ui(prop).update(min=0,max=1,description='0 FK, 1 IK. Use Match IK / Match FK to preserve pose.')
    lower=rig.pose.bones['MCH_'+key+'_1'];ik=lower.constraints.new('IK');ik.name='Two bone IK / no stretch';ik.target=rig;ik.subtarget=info['target'];ik.pole_target=rig;ik.pole_subtarget='POLE_'+key;ik.chain_count=2;ik.use_stretch=False
    for i in [0,1]:rig.pose.bones['MCH_'+key+'_'+str(i)].ik_stretch=0
    end=rig.pose.bones['MCH_'+key+'_2'];c=end.constraints.new('COPY_ROTATION');c.target=rig;c.subtarget=info['target'];c.owner_space='WORLD';c.target_space='WORLD'
    # Fit the pole angle against the inspected rest bend; then refine.
    desired=Vector(info['mid']);best=(1e9,0)
    for step in [math.tau/32,math.tau/512,math.tau/8192]:
        center=best[1];angles=[-math.pi+i*step for i in range(32)] if best[0]==1e9 else [center+i*step for i in range(-16,17)]
        for angle in angles:
            ik.pole_angle=angle;bpy.context.view_layer.update();error=(lower.head-desired).length
            if error<best[0]:best=(error,angle)
    ik.pole_angle=best[1];audit['limbs'][key]={'rest_mid_error_cm':best[0]*100,'pole_angle':best[1]}
for bone in a.data.bones:
    p=a.pose.bones[bone.name];c=p.constraints.new('COPY_TRANSFORMS');c.name='Authoring FK';c.target=rig;c.subtarget='FK_'+bone.name;c.owner_space='LOCAL';c.target_space='LOCAL'
    for key,info in limbs.items():
        if bone.name not in info['names']:continue
        c=p.constraints.new('COPY_TRANSFORMS');c.name='Authoring IK';c.target=rig;c.subtarget='RES_'+bone.name;c.owner_space='WORLD';c.target_space='WORLD'
        f=c.driver_add('influence');d=f.driver;d.type='AVERAGE';v=d.variables.new();v.name='blend';v.type='SINGLE_PROP';v.targets[0].id=rig;v.targets[0].data_path='pose.bones["FK_root"]["ik_'+key+'"]'
for marker,target in [('ik_foot_l','foot_l'),('ik_foot_r','foot_r'),('ik_hand_l','hand_l'),('ik_hand_r','hand_r')]:
    c=a.pose.bones[marker].constraints.new('COPY_TRANSFORMS');c.name='Authoring solved limb marker';c.target=a;c.subtarget=target;c.owner_space='WORLD';c.target_space='WORLD'
gun=a.pose.bones['ik_hand_gun'];c=gun.constraints.new('COPY_TRANSFORMS');c.name='Authoring wrist reference';c.target=a;c.subtarget='hand_r';c.owner_space='WORLD';c.target_space='WORLD'
f=c.driver_add('influence');d=f.driver;d.expression='1-independent';v=d.variables.new();v.name='independent';v.type='SINGLE_PROP';v.targets[0].id=rig;v.targets[0].data_path='pose.bones["FK_root"]["weapon_independent"]'
a.hide_set(True);a.show_in_front=False
bpy.context.view_layer.update()
open(BASE+'/Work/control_build_audit.json','w').write(json.dumps(audit,indent=2))
result=audit
