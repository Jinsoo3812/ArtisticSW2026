"""Validate every authored clip frame and save reproducible viewport diagnostics."""
import bpy
import json
from pathlib import Path
from mathutils import Vector
from mathutils.bvhtree import BVHTree

ROOT = Path(__file__).resolve().parents[2] / "SourceArt/Characters/YiSunSin"
scene = bpy.context.scene
arm = bpy.data.objects['Armature']
work = bpy.data.objects['SK_YiSunSin_NPC']
reference = bpy.data.objects['REF_CompleteRootWeights']
preparation = json.loads((ROOT / 'Audit/preparation.json').read_text(encoding='utf-8'))
triangles = [tuple(p.vertices) for p in work.data.polygons]
upper_pants = [p.index for p in work.data.polygons if p.material_index == 5 and
               sum((reference.matrix_world @ reference.data.vertices[i].co).z
                   for i in p.vertices)/len(p.vertices) > 0.68][::5]
directions = [Vector(d).normalized() for d in [(0,-1,.08), (1,0,.08), (0,1,.08), (-1,0,.08)]]
garment_ids = {i for p in work.data.polygons if p.material_index in {3,4} for i in p.vertices}
changed_geometry = []
changed_weights = []
for a,b in zip(work.data.vertices, reference.data.vertices):
    if (a.co-b.co).length > 1e-6:
        changed_geometry.append(a.index)
    wa = {work.vertex_groups[g.group].name: g.weight for g in a.groups}
    wb = {reference.vertex_groups[g.group].name: g.weight for g in b.groups}
    if max((abs(wa.get(n,0)-wb.get(n,0)) for n in set(wa)|set(wb)), default=0) > 1e-6:
        changed_weights.append(a.index)
assert set(changed_geometry) <= garment_ids
assert set(changed_weights) <= garment_ids
weight_error = max(abs(sum(g.weight for g in v.groups)-1) for v in work.data.vertices)
assert weight_error < 1e-5, weight_error
rows = []
max_bone_position_error = 0.0
max_bone_rotation_error = 0.0
bpy.data.collections['02_Animation_Reference'].hide_viewport = False
reference.hide_set(False)
arm.hide_set(False)
for name, info in preparation['animations'].items():
    arm.animation_data.action = bpy.data.actions[info['action']]
    arm.animation_data.action_slot = arm.animation_data.action.slots[0]
    source = bpy.data.objects[info['source']]
    source.hide_set(False)
    for frame in range(info['start'], info['end']+1):
        scene.frame_set(frame)
        for bone in source.pose.bones:
            expected = source.matrix_world @ bone.matrix
            actual = arm.matrix_world @ arm.pose.bones[bone.name].matrix
            max_bone_position_error = max(max_bone_position_error,
                                          (actual.translation-expected.translation).length)
            max_bone_rotation_error = max(max_bone_rotation_error,
                                          actual.to_quaternion().rotation_difference(expected.to_quaternion()).angle)
        expected = source.matrix_world
        actual = arm.matrix_world @ arm.pose.bones['spine'].matrix
        max_bone_position_error = max(max_bone_position_error, (actual.translation-expected.translation).length)
        row = {'clip': name, 'frame': frame}
        for label, obj in [('before', reference), ('after', work)]:
            evaluated = obj.evaluated_get(bpy.context.evaluated_depsgraph_get())
            mesh = evaluated.to_mesh()
            points = [obj.matrix_world @ v.co for v in mesh.vertices]
            tree = BVHTree.FromPolygons(points, triangles, all_triangles=True)
            visible = 0
            for face in upper_pants:
                point = sum((points[i] for i in triangles[face]), Vector())/3
                for direction in directions:
                    hit, normal, index, distance = tree.ray_cast(point+direction*5, -direction, 5.0001)
                    visible += index == face
            row[label + '_upper_pants_visible_samples'] = visible
            evaluated.to_mesh_clear()
        rows.append(row)
assert max_bone_position_error < 0.00002, max_bone_position_error
report = {'sampled_frames': len(rows), 'bone_position_error_m': max_bone_position_error,
          'bone_rotation_error_radians': max_bone_rotation_error, 'weight_sum_error': weight_error,
          'changed_geometry_vertices': len(changed_geometry), 'changed_weight_vertices': len(changed_weights),
          'untouched_body_hair_hat_hands_pants_shoes_sash': True, 'removed_faces': 0,
          'visibility_test': 'Four orthographic directions, every fifth pants triangle centroid above 0.68m in reference pose. Includes intended slit visibility; not a collision-free proof.',
          'rows': rows}
(ROOT / 'Audit/all_frames_validation.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
result = {k:v for k,v in report.items() if k != 'rows'}
result['visibility_before_total'] = sum(r['before_upper_pants_visible_samples'] for r in rows)
result['visibility_after_total'] = sum(r['after_upper_pants_visible_samples'] for r in rows)
result['worst_after'] = sorted(rows, key=lambda r:r['after_upper_pants_visible_samples'], reverse=True)[:8]
