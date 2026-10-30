"""Fit the NPC robe skin to the existing leg skin, retaining the original rig.

Run on the working copy only. The untouched reference provides all donor data.
"""
import bpy
import json
from math import exp
from mathutils import Vector
from pathlib import Path
from mathutils.bvhtree import BVHTree

ROOT = Path(__file__).resolve().parents[2] / "SourceArt/Characters/YiSunSin"
work = bpy.data.objects['SK_YiSunSin_NPC']
reference = bpy.data.objects['REF_CompleteRootWeights']
assert len(work.data.vertices) == len(reference.data.vertices)
positions = [reference.matrix_world @ v.co for v in reference.data.vertices]
triangles = [tuple(p.vertices) for p in reference.data.polygons if p.material_index == 5]
tree = BVHTree.FromPolygons(positions, triangles, all_triangles=True)
garment_ids = {i for p in work.data.polygons if p.material_index in {3, 4} for i in p.vertices}
garment_materials = {i: p.material_index for p in work.data.polygons
                     if p.material_index in {3, 4} for i in p.vertices}
groups = {g.index: g.name for g in reference.vertex_groups}
donor_weights = [{groups[g.group]: g.weight for g in v.groups} for v in reference.data.vertices]
for group in reference.vertex_groups:
    if not work.vertex_groups.get(group.name):
        work.vertex_groups.new(name=group.name)

def barycentric(point, a, b, c):
    v0, v1, v2 = b-a, c-a, point-a
    d00, d01, d11 = v0.dot(v0), v0.dot(v1), v1.dot(v1)
    denom = d00*d11 - d01*d01
    if abs(denom) < 1e-16:
        return (1.0, 0.0, 0.0)
    v = (d11*v2.dot(v0)-d01*v2.dot(v1))/denom
    w = (d00*v2.dot(v1)-d01*v2.dot(v0))/denom
    values = [max(0.0, 1-v-w), max(0.0, v), max(0.0, w)]
    total = sum(values)
    return [value/total for value in values]

for group in work.vertex_groups:
    group.remove(list(range(len(work.data.vertices))))
for index, weights in enumerate(donor_weights):
    work.data.vertices[index].co = reference.data.vertices[index].co
    for name, weight in weights.items():
        work.vertex_groups[name].add([index], weight, 'REPLACE')
changed = 0
clearance_changed = 0
max_clearance = 0.0
for index in sorted(garment_ids):
    point = positions[index]
    if point.z >= 1.28 or abs(point.x) > 0.34:
        continue
    sample = point.copy()
    # The long hem hangs from the upper leg; copying ankle/toe rotation folds it.
    sample.z = max(sample.z, 0.66)
    hit, normal, face, distance = tree.find_nearest(sample)
    tri = triangles[face]
    factors = barycentric(hit, *[positions[i] for i in tri])
    weights = {}
    for i, factor in zip(tri, factors):
        for name, weight in donor_weights[i].items():
            weights[name] = weights.get(name, 0.0) + factor*weight
    blend = max(0.0, min(1.0, (1.28-point.z)/0.18))
    blend = blend*blend*(3-2*blend)
    original = donor_weights[index]
    weights = {name: blend*weights.get(name, 0)+(1-blend)*original.get(name, 0)
               for name in set(weights) | set(original)}
    total = sum(weights.values())
    for group in work.vertex_groups:
        group.remove([index])
    for name, weight in weights.items():
        if weight > 1e-8:
            work.vertex_groups[name].add([index], weight/total, 'REPLACE')
    # Retain a layered robe silhouette with authored rest clearance, not runtime scale.
    fit = max(0.0, min(1.0, (1.10-point.z)/0.20))
    fit = fit*fit*(3-2*fit)
    factor = 1 + fit*(0.18 if garment_materials[index] == 4 else 0.10)
    factor += (0.035 if garment_materials[index] == 4 else 0.020)*exp(-((point.z-1.09)/0.07)**2)
    fitted = Vector((point.x*factor, 0.02+(point.y-0.02)*factor, point.z))
    work.data.vertices[index].co = work.matrix_world.inverted() @ fitted
    if fit > 0:
        clearance_changed += 1
        max_clearance = max(max_clearance, (fitted-point).length)
    changed += 1
work.data.update()
result = {'method': 'nearest triangle barycentric leg skin transfer with smooth waist transition',
          'changed_garment_vertices': changed, 'clearance_vertices': clearance_changed,
          'maximum_rest_clearance_m': max_clearance,
          'waist_blend_m': [1.10, 1.28], 'lower_donor_sample_height_min_m': 0.66,
          'added_bones': 0, 'removed_faces': 0,
          'reference_untouched': True}
(ROOT / 'Audit/garment_fit.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
