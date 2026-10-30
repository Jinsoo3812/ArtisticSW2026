"""Skin the authored mesh using captured UE runtime poses and inspect garment coverage.
Uses original bind matrices and the saved mesh's influences; creates no scene objects.
"""
import bpy,json,numpy as np
from pathlib import Path
from mathutils import Matrix,Quaternion,Vector
from mathutils.bvhtree import BVHTree
ROOT=Path(__file__).resolve().parents[2]/'SourceArt/Characters/YiSunSin'
data=json.loads((ROOT/'Audit/ue_blend_samples.json').read_text())
assert data['passed'],data.get('error')
obj=bpy.data.objects['SK_YiSunSin_NPC']; reference=bpy.data.objects['REF_CompleteRootWeights']

def mat(t):
    return np.array(Matrix.LocRotScale(Vector(t['p']),Quaternion((t['q'][3],*t['q'][:3])),Vector(t['s'])),dtype=float)
verts=np.array([list(obj.matrix_world@v.co)+[1] for v in obj.data.vertices])
verts[:,:3]*=np.array([100,-100,100])
refs={n:np.linalg.inv(mat(t)) for n,t in data['reference_component'].items()}
influences={g.index:[g.name,[],[]] for g in obj.vertex_groups}
for v in obj.data.vertices:
    for g in v.groups:
        if g.weight>0:
            item=influences[g.group];item[1].append(v.index);item[2].append(g.weight)
influences=[(name,np.array(indices),np.array(weights)[:,None]) for name,indices,weights in influences.values() if indices]
triangles=[tuple(p.vertices) for p in obj.data.polygons]
upper=[p.index for p in obj.data.polygons if p.material_index==5 and sum((reference.matrix_world@reference.data.vertices[i].co).z for i in p.vertices)/len(p.vertices)>.68][::5]
directions=[Vector(d).normalized() for d in [(0,-1,.08),(1,0,.08),(0,1,.08),(-1,0,.08)]]
rows=[]
# Every captured pose includes all bones. Check all captured samples, including
# actual acceleration/deceleration values, rather than approximating UE blending.
for sample in data['rows']:
    posed=np.zeros((len(verts),3))
    for name,indices,weights in influences:
        transform=mat(sample['bones'][name])@refs[name]
        posed[indices]+=(verts[indices]@transform.T)[:,:3]*weights
    posed*=np.array([.01,-.01,.01])
    points=[Vector(p) for p in posed]
    tree=BVHTree.FromPolygons(points,triangles,all_triangles=True)
    visible=0
    for face in upper:
        point=sum((points[i] for i in triangles[face]),Vector())/3
        for direction in directions:
            _,_,index,_=tree.ray_cast(point+direction*5,-direction,5.0001)
            visible+=index==face
    rows.append({'time':sample['time'],'speed':sample['speed'],'actual_speed':sample['actual_speed'],
                 'upper_pants_visible_samples':visible,'mesh_height_m':float(np.ptp(posed[:,2]))})
    if len(rows)%20==0:
        (ROOT/'Audit/blend_geometry_progress.json').write_text(json.dumps({'completed':len(rows),'total':len(data['rows'])}))
report={'samples':len(rows),'using_actual_ue_component_bone_transforms':True,
        'visibility_test':'Four directions; every fifth pants triangle centroid with reference height >0.68m. Does not prove all garment surfaces never intersect.',
        'upper_pants_visible_total':sum(r['upper_pants_visible_samples'] for r in rows),
        'worst_upper_pants_visible':max(r['upper_pants_visible_samples'] for r in rows),'rows':rows}
(ROOT/'Audit/blend_geometry_validation.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
result={k:v for k,v in report.items() if k!='rows'}
