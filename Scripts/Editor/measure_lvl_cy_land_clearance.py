"""Read-only full-candidate clearance audit, independent of random activation."""
import json
import math
import runpy
import time
import traceback
from pathlib import Path
import unreal

ROOT = Path(unreal.Paths.project_dir())
OUT = ROOT / 'Saved/LandRandomAudit'
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
if '-VerifyZFix' in unreal.SystemLibrary.get_command_line():
    runpy.run_path(str(ROOT / 'Scripts/Editor/audit_lvl_cy_land_random.py'))
unreal.EditorLoadingAndSavingUtils.load_map('/Game/Level/Lvl_CY')
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
types = [unreal.ObjectTypeQuery.OBJECT_TYPE_QUERY1, unreal.ObjectTypeQuery.OBJECT_TYPE_QUERY2]
block = getattr(unreal.CollisionResponseType, 'ECR_BLOCK', None) or unreal.CollisionResponseType.BLOCK

def v(a):
    return [a.x, a.y, a.z]

def hit_info(start, end, ignore, complex_trace=False):
    hit = unreal.SystemLibrary.line_trace_single_for_objects(world, start, end, types, complex_trace,
        ignore + list(unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Volume)),
        unreal.DrawDebugTrace.NONE, True)
    if isinstance(hit, tuple):
        hit = hit[-1]
    if not hit:
        return None
    h = hit.to_tuple()
    if not h[0]:
        return None
    return {'point': v(h[5]), 'normal': v(h[7]), 'initial_overlap': h[1],
            'actor': h[9].get_actor_label() if h[9] else None,
            'component': h[10].get_path_name() if h[10] else None}

def measure(points):
    rows = []
    for p in sorted(points, key=lambda a: int(a.get_actor_label().rsplit('_', 1)[1])):
        selected = p.is_activated()
        chest = p.get_spawned_actor() or p.spawn_configured_chest(None, 20261027)
        assert chest, p.get_actor_label()
        mesh = chest.get_editor_property('chest_mesh')
        tr = mesh.get_world_transform()
        center, extent, radius = unreal.SystemLibrary.get_component_bounds(mesh)
        bottom = center.z - extent.z
        overlaps = {}
        for offset in [0, 3, 10, 25, 50, 100, 200, 500, 1000]:
            test_tr = unreal.Transform(location=tr.translation + unreal.Vector(0, 0, offset),
                rotation=tr.rotation.rotator(), scale=tr.scale3d)
            result = unreal.SystemLibrary.component_overlap_components(mesh, test_tr, types,
                unreal.PrimitiveComponent, [p, chest])
            if isinstance(result, tuple):
                result = result[-1]
            overlaps[str(offset)] = [{'component': c.get_path_name(), 'actor': c.get_owner().get_actor_label()}
                for c in result or [] if c.get_collision_response_to_channel(mesh.get_collision_object_type()) == block
                and mesh.get_collision_response_to_channel(c.get_collision_object_type()) == block]
        supports = []
        for dx, dy in [(0, 0), (-0.8, -0.8), (-0.8, 0.8), (0.8, -0.8), (0.8, 0.8)]:
            xy = unreal.Vector(center.x + dx * extent.x, center.y + dy * extent.y, bottom)
            supports.append({'simple': hit_info(xy + unreal.Vector(0, 0, 2000), xy - unreal.Vector(0, 0, 2000), [p, chest]),
                             'complex': hit_info(xy + unreal.Vector(0, 0, 2000), xy - unreal.Vector(0, 0, 2000), [p, chest], True)})
        rows.append({'label': p.get_actor_label(), 'randomly_selected': selected,
            'support_probe_excludes_volumes': True,
            'point_location': v(p.get_actor_location()), 'chest_location': v(chest.get_actor_location()),
            'mesh_center': v(center), 'extent': v(extent), 'bottom': bottom,
            'overlaps_by_lift_cm': overlaps, 'supports_from_above': supports})
        if '-PlanZFix' in unreal.SystemLibrary.get_command_line():
            water = [a for a in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Actor)
                     if 'WaterBody' in a.get_class().get_name()]
            land = hit_info(unreal.Vector(center.x, center.y, bottom + 2000),
                            unreal.Vector(center.x, center.y, bottom - 5000), [p, chest] + water)
            rows[-1]['dry_ground_probe'] = land
            if not land or land['initial_overlap'] or land['point'][2] < 0:
                rows[-1]['z_fix'] = {'status': 'unresolved_no_dry_ground_at_xy'}
                continue
            def blocked_at(delta):
                test = unreal.Transform(location=tr.translation + unreal.Vector(0, 0, delta),
                    rotation=tr.rotation.rotator(), scale=tr.scale3d)
                cs = unreal.SystemLibrary.component_overlap_components(mesh, test, types,
                    unreal.PrimitiveComponent, [p, chest])
                if isinstance(cs, tuple):
                    cs = cs[-1]
                return any(c.get_collision_response_to_channel(mesh.get_collision_object_type()) == block
                    and mesh.get_collision_response_to_channel(c.get_collision_object_type()) == block for c in cs or [])
            base = max(0.0, land['point'][2] + 1 - bottom)
            delta = base
            while blocked_at(delta) and delta < base + 1000:
                delta += 1.0
            assert not blocked_at(delta), p.get_actor_label()
            if delta > 0.1:
                delta = math.ceil(delta + 1.0)
            old_clearance = p.get_editor_property('ground_clearance')
            old_z = p.get_actor_location().z
            new_clearance = old_clearance if delta <= 0.1 else round(bottom + delta - land['point'][2], 3)
            new_z = max(old_z, land['point'][2] + 50) if land['point'][2] >= old_z + 200 else old_z
            rows[-1]['z_fix'] = {'status': 'raise' if delta > 0.1 else 'unchanged',
                'old_point_z': old_z, 'new_point_z': new_z,
                'old_ground_clearance': old_clearance, 'new_ground_clearance': new_clearance,
                'predicted_chest_lift_cm': delta, 'target_chest_z': chest.get_actor_location().z + delta}
    output_name = 'z_fix_plan.json' if '-PlanZFix' in unreal.SystemLibrary.get_command_line() else (
        'clearance_after_z_fix.json' if '-VerifyZFix' in unreal.SystemLibrary.get_command_line() else 'clearance_all27.json')
    (OUT / output_name).write_text(json.dumps(rows, ensure_ascii=False, indent=2), encoding='utf-8')
    unreal.log('LAND_RANDOM_CLEARANCE_COMPLETE count=%d' % len(rows))

stage = 'start'
deadline = time.monotonic() + 240
def tick(delta):
    global world, stage
    try:
        if time.monotonic() > deadline:
            raise RuntimeError('Clearance PIE timeout')
        if stage == 'start':
            level.editor_request_begin_play()
            stage = 'playing'
            return
        world = editor.get_game_world()
        if not world or not level.is_in_play_in_editor():
            return
        for pool in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.StorageChestPool):
            pool.set_actor_tick_enabled(False)
        if unreal.GameplayStatics.get_time_seconds(world) < 5:
            return
        points = [p for p in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.ChestSpawnPoint)
                  if p.get_actor_label().startswith('BP_ChestSpawnPoint_Land_Random_')]
        assert len(points) == (26 if '-VerifyZFix' in unreal.SystemLibrary.get_command_line() else 27)
        measure(points)
    except Exception:
        (OUT / 'clearance_error.txt').write_text(traceback.format_exc(), encoding='utf-8')
    else:
        pass
    unreal.unregister_slate_post_tick_callback(handle)
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)
    unreal.SystemLibrary.quit_editor()

handle = unreal.register_slate_post_tick_callback(tick)
