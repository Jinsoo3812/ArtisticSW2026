"""Bake a forward swing correction against the animated robe/leg front envelope.
The correction affects the sash chain only, leaving all body animation intact.
"""
import bpy,json,math,numpy as np
from pathlib import Path
from mathutils import Matrix,Vector
from mathutils.bvhtree import BVHTree
ROOT=Path(__file__).resolve().parents[2]/'SourceArt/Characters/YiSunSin'
obj=bpy.data.objects['SK_YiSunSin_NPC'];ref=bpy.data.objects['REF_CompleteRootWeights'];arm=bpy.data.objects['Armature'];scene=bpy.context.scene
faces=[tuple(p.vertices) for p in obj.data.polygons if p.material_index in {3,4,5}]
ids=sorted({i for p in obj.data.polygons if p.material_index==0 for i in p.vertices if (ref.matrix_world@ref.data.vertices[i].co).z<1.15})
root_group=obj.vertex_groups['spine'].index
anchors={i:1-sum(g.weight for g in obj.data.vertices[i].groups if g.group==root_group) for i in ids}
report=[]
for label,end in [('Idle_Upright',122),('Walk',36),('Run',26)]:
    action=bpy.data.actions['NPC_Sash_'+label]
    assert not action.get('sash_collision_baked'), 'Rerun author_yisunsin_sash.py to restore the unsolved clips first'
    arm.animation_data.action=action;arm.animation_data.action_slot=action.slots[0]
    required=[];matrices=[]
    for frame in range(1,end+1):
        scene.frame_set(frame);bpy.context.view_layer.update()
        e=obj.evaluated_get(bpy.context.evaluated_depsgraph_get());mesh=e.to_mesh();pts=[obj.matrix_world@v.co for v in mesh.vertices]
        tree=BVHTree.FromPolygons(pts,faces,all_triangles=True)
        worlds=[arm.matrix_world@arm.pose.bones['npc_sash_%02d'%i].matrix for i in range(1,5)]
        matrices.append([w.copy() for w in worlds])
        root_deform=(arm.matrix_world@arm.pose.bones['spine'].matrix)@(arm.matrix_world@arm.data.bones['spine'].matrix_local).inverted()
        root_parts={i:(root_deform@(obj.matrix_world@obj.data.vertices[i].co))*(1-anchors[i]) for i in ids}
        weighted=[]
        for index in range(4):
            name='npc_sash_%02d'%(index+1);group=obj.vertex_groups[name].index
            deform=worlds[index]@(arm.matrix_world@arm.data.bones[name].matrix_local).inverted()
            weighted.append([(deform@(obj.matrix_world@obj.data.vertices[i].co),sum(g.weight for g in obj.data.vertices[i].groups if g.group==group)) for i in ids])
        def corrected(worlds,degrees):
            result=[];cumulative=Matrix.Identity(4)
            for index,world in enumerate(worlds):
                current=cumulative@world;pivot=current.translation
                angle=-degrees if index==0 else degrees*.5 if index==1 else degrees*.25 if index==2 else 0
                correction=Matrix.Translation(pivot)@Matrix.Rotation(math.radians(angle),4,'X')@Matrix.Translation(-pivot)
                cumulative=correction@cumulative;result.append(cumulative.copy())
            return result
        def minimum_gap(degrees):
            corrections=corrected(worlds,degrees);worst=10.0
            for position,i in enumerate(ids):
                if anchors[i]<.5:continue
                point=root_parts[i].copy()
                for index in range(4):
                    source,weight=weighted[index][position]
                    if weight:point+=(corrections[index]@source)*weight
                hit,_,_,_=tree.ray_cast(Vector((point.x,-3,point.z)),Vector((0,1,0)),6)
                if hit:
                    ref_z=(ref.matrix_world@ref.data.vertices[i].co).z
                    margin=.025 if label=='Idle_Upright' else .035 if ref_z>=1.08 else .16
                    worst=min(worst,hit.y-point.y-margin)
            return worst
        value=None
        for angle in range(0,86,2):
            if minimum_gap(angle)>=0: value=angle;break
        if value is None: raise RuntimeError('No collision-safe sash swing for '+label+' frame '+str(frame))
        required.append(value)
        e.to_mesh_clear()
    # A circular look-ahead envelope softens the approach to raised knees.
    count=end-1
    required[0]=required[-1]=max(required[0],required[-1])
    smooth=[]
    for i in range(end):
        smooth.append(max(required[i],max(required[(i+j)%count]-2*abs(j) for j in range(-5,6))))
    smooth[0]=smooth[-1]=max(smooth[0],smooth[-1])
    for frame,angle,worlds in zip(range(1,end+1),smooth,matrices):
        scene.frame_set(frame)
        corrections=corrected(worlds,angle)
        for index,world in enumerate(worlds):
            pose=arm.pose.bones['npc_sash_%02d'%(index+1)]
            pose.matrix=arm.matrix_world.inverted()@corrections[index]@world
            bpy.context.view_layer.update()
            for prop in ['location','rotation_quaternion','scale']:pose.keyframe_insert(data_path=prop,frame=frame,group=pose.name)
    action['sash_collision_baked']=True
    report.append({'clip':label,'frames':end,'maximum_extra_swing_degrees':max(smooth),'minimum_extra_swing_degrees':min(smooth),'swing_profile_degrees':smooth})
(ROOT/'Audit/sash_collision_authoring.json').write_text(json.dumps({'method':'Baked four-bone counter-curved swing with animated front envelope and 25mm idle / 35mm attachment / 160mm lower gait clearance targets','clips':report},indent=2))
arm.animation_data.action=bpy.data.actions['NPC_Sash_Idle_Upright'];arm.animation_data.action_slot=arm.animation_data.action.slots[0];scene.frame_set(1)
bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/'YiSunSin_NPC.blend'))
result={'clips':[{k:v for k,v in r.items() if k!='swing_profile_degrees'} for r in report]}
