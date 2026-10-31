"""Fit low-complexity world-physics proxies to the delivered, weighted character surfaces.

Read-only for the source rigs. Produces component-space cm shapes and anatomical joint
frames; Unreal converts these to the actual mesh's bone space when authoring each PA.
"""
import bpy,json,math
from pathlib import Path
from mathutils import Vector,Matrix
ROOT=Path(r'C:\Users\I\Documents\GitHub\ArtisticSW2026');OUT=ROOT/'SourceArt/Characters/Physics'
roles=['pelvis','spine_02','spine_04','neck_01','head']
for side in ['l','r']:roles += [n+'_'+side for n in ['upperarm','lowerarm','hand','thigh','calf','foot']]
def quantile(values,p):
    values=sorted(values);index=(len(values)-1)*p;a=int(index);b=min(a+1,len(values)-1);return values[a]+(values[b]-values[a])*(index-a)
def axes(x,y):
    x=x.normalized();y=y-x*x.dot(y)
    if y.length<.01:y=Vector((0,0,1))-x*x.z
    y.normalize();z=x.cross(y).normalized();return Matrix((x,y,z)).transposed()
def load_man():
    with bpy.data.libraries.load(str(ROOT/'SourceArt/Characters/ChMan/ChMan_UE5_Rig.blend'),link=False) as (source,target):target.objects=['Armature','SKM_ChMan_UE5']
    return target.objects
man_a,man_m=load_man()
for label,a,m,asset,total_mass in [('Man',man_a,man_m,'ChManUE5/SKM_ChMan_UE5',72),('Woman',bpy.data.objects['Woman_EXPORT'],bpy.data.objects['SKM_ChWoman_UE5'],'ChWomanUE5/SKM_ChWoman_UE5',58)]:
    def ue(v):return Vector((v.x*100,-v.y*100,v.z*100))
    joints={b.name:ue(a.matrix_world@b.head_local) for b in a.data.bones}
    def role(name):
        if 'breast' in name or 'ribstwist' in name:return 'spine_04'
        b=a.data.bones.get(name)
        while b:
            if b.name in roles:return b.name
            b=b.parent
        return None
    group_roles={g.index:role(g.name) for g in m.vertex_groups}
    allowed={i for i,mat in enumerate(m.data.materials) if mat and any(s in mat.name.lower() for s in ['body','clothes','shoes'])}
    vertices={i for p in m.data.polygons if p.material_index in allowed for i in p.vertices};cloud={n:[] for n in roles}
    for index in vertices:
        v=m.data.vertices[index];weights={}
        for g in v.groups:
            n=group_roles.get(g.group)
            if n:weights[n]=weights.get(n,0)+g.weight
        if weights:cloud[max(weights,key=weights.get)].append(ue(m.matrix_world@v.co))
    descriptor={'mesh':'/Game/jiwon/Characters/'+asset,'mass_kg':total_mass,'bodies':[],'joints':[],'source':'actual weighted body/clothes/shoes, hair/accessories/bag excluded','units':'cm','solver':'world Chaos, not AnimGraph RigidBody'}
    descriptor['world_solver']={'position_iterations':64,'velocity_iterations':16,'projection_iterations':1,'projection_linear_alpha':.05,'projection_angular_alpha':.05,'projection_linear_tolerance_cm':3,'projection_angular_tolerance_deg':8,'linear_damping':.35,'angular_damping':2.2}
    masses={'pelvis':14,'spine_02':9,'spine_04':14,'neck_01':1.2,'head':5,'upperarm':2.5,'lowerarm':1.6,'hand':.6,'thigh':7.5,'calf':3.8,'foot':1.4}
    total=sum(masses.get(n,masses.get(n.rsplit('_',1)[0])) for n in roles)
    for n in roles:
        pts=cloud[n];assert len(pts)>5,(n,len(pts));p=joints[n];stem=n.rsplit('_',1)[0];side=n[-1]
        mass=masses[n] if n in masses else masses[stem]
        body={'bone':n,'mass_kg':total_mass*mass/total,'fit_vertices':len(pts)}
        if stem in ['upperarm','lowerarm','thigh','calf']:
            end=joints[{'upperarm':'lowerarm','lowerarm':'hand','thigh':'calf','calf':'foot'}[stem]+'_'+side];direction=(end-p).normalized();length=(end-p).length
            radial=[(v-p-direction*(v-p).dot(direction)).length for v in pts if .15*length<(v-p).dot(direction)<.85*length]
            radius=quantile(radial or [(v-p).length for v in pts],.80)*.88;radius=max(1.5,min(radius,length*.22))
            q=Vector((0,0,1)).rotation_difference(direction)
            body.update(shape='capsule',center=list((p+end)/2),rotation=list(q),radius=radius,length=max(.5,length-2*radius))
        elif n=='neck_01':
            end=joints['head'];direction=(end-p).normalized();length=(end-p).length;radius=min(length*.3,3.6)
            body.update(shape='capsule',center=list((p+end)/2),rotation=list(Vector((0,0,1)).rotation_difference(direction)),radius=radius,length=max(.5,length-2*radius))
        else:
            basis=Matrix.Identity(3)
            if stem=='hand':
                direction=joints['middle_03_'+side]-p;width=joints['index_01_'+side]-joints['pinky_01_'+side];basis=axes(direction,width)
            elif stem=='foot':
                forward=joints['ball_'+side]-p;forward.z=0;forward.normalize();basis=axes(Vector((0,0,1)).cross(forward),-forward)
            local=[basis.transposed()@v for v in pts];low=Vector([quantile([v[i] for v in local],.015) for i in range(3)]);high=Vector([quantile([v[i] for v in local],.985) for i in range(3)])
            center=basis@((low+high)/2);dimensions=(high-low)*.9
            if n=='head':
                radius=min(dimensions.x,dimensions.y)*.5;body.update(shape='capsule',center=list(center),rotation=[1,0,0,0],radius=radius,length=max(.5,dimensions.z-2*radius))
            else:body.update(shape='box',center=list(center),rotation=list(basis.to_quaternion()),dimensions=[max(1.5,v) for v in dimensions])
        descriptor['bodies'].append(body)
    parents={'spine_02':'pelvis','spine_04':'spine_02','neck_01':'spine_04','head':'neck_01'}
    for side in ['l','r']:
        for child,parent in [('upperarm','spine_04'),('lowerarm','upperarm'),('hand','lowerarm'),('thigh','pelvis'),('calf','thigh'),('foot','calf')]:parents[child+'_'+side]=parent if parent in ['spine_04','pelvis'] else parent+'_'+side
    for child,parent in parents.items():
        p=joints[child];stem=child.rsplit('_',1)[0];side=child[-1];bias=[0,0,0];kind='cone';bend=None
        if stem in ['lowerarm','calf']:
            start=joints[('upperarm' if stem=='lowerarm' else 'thigh')+'_'+side];end=joints[('hand' if stem=='lowerarm' else 'foot')+'_'+side]
            u=(p-start).normalized();v=(end-p).normalized();hinge=u.cross(v)
            if hinge.length<.001:hinge=Vector((-1,0,0))
            hinge.normalize();basis=axes(v,hinge);bend=math.degrees(math.atan2(hinge.dot(u.cross(v)),u.dot(v)))
            minimum=0;maximum=50 if stem=='calf' else 40
            lo=minimum-bend;hi=maximum-bend;bias[1]=(hi+lo)/2
            # Longitudinal twist is distinct from flexion: the forearm needs
            # pronation, the knee only a small amount of axial freedom.
            limits=[3,(hi-lo)/2,45 if stem=='lowerarm' else 5];kind='elbow' if stem=='lowerarm' else 'knee'
        else:
            end={'spine_02':'spine_04','spine_04':'neck_01','neck_01':'head'}.get(child)
            if stem in ['upperarm','thigh']:end=('lowerarm' if stem=='upperarm' else 'calf')+'_'+side
            if stem in ['hand','foot']:end=('middle_03' if stem=='hand' else 'ball')+'_'+side
            direction=(joints[end]-p) if end else Vector((0,0,1))
            secondary=Vector((1,0,0))
            if stem=='hand':secondary=joints['index_01_'+side]-joints['pinky_01_'+side]
            basis=axes(direction,secondary)
            if child=='spine_02':limits=[10,18,8];bias[1]=5
            elif child=='spine_04':limits=[12,18,10];bias[1]=5
            elif child=='neck_01':limits=[12,18,15]
            elif child=='head':limits=[10,12,15]
            elif stem=='upperarm':limits=[55,65,25]
            elif stem=='thigh':limits=[30,50,20];bias[1]=35
            elif stem=='hand':limits=[15,25,20]
            else:limits=[12,22.5,8];bias[1]=-7.5
        descriptor['joints'].append({'child':child,'parent':parent,'pivot':list(p),'rotation':list(basis.to_quaternion()),'bias_xyz_deg':bias,'swing1_swing2_twist_deg':limits,'kind':kind,'rest_bend_deg':bend})
    (OUT/('Ch'+label+'_Physics.json')).write_text(json.dumps(descriptor,indent=2),encoding='utf8')
result={'profiles':[{n:label for n,label in [('name',label)]} for label in ['Man','Woman']],'body_count':17,'joint_count':16}
