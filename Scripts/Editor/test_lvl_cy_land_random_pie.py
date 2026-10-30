"""Repeat real PIE starts; retain machine-readable chest and collision evidence."""
import json
import math
import time
import traceback
from pathlib import Path
import unreal

unreal.EditorPythonScripting.set_keep_python_script_alive(True)

OUT = Path(unreal.Paths.project_saved_dir()) / 'LandRandomAudit'
OUT.mkdir(parents=True, exist_ok=True)
VERIFY_Z = '-VerifyZFix' in unreal.SystemLibrary.get_command_line()
EXPECTED_COUNTS = [5, 5, 3, 8] if VERIFY_Z else [5, 5, 4, 8]
RESULT = {'runs': [], 'errors': [], 'method': 'real PIE BeginPlay; saved Lvl_CY; random seeds; actual mesh overlap and support traces'}
ZONES = [unreal.ProgressionZone.MID1, unreal.ProgressionZone.MID2,
         unreal.ProgressionZone.MID3, unreal.ProgressionZone.FINAL]
TYPES = [unreal.ObjectTypeQuery.OBJECT_TYPE_QUERY1, unreal.ObjectTypeQuery.OBJECT_TYPE_QUERY2]
BLOCK = getattr(unreal.CollisionResponseType, 'ECR_BLOCK', None) or unreal.CollisionResponseType.BLOCK
LEVEL = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
EDITOR = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)

def vec(v):
    return [v.x, v.y, v.z]

def named(obj):
    return {'name': obj.get_name(), 'path': obj.get_path_name(),
            'label': obj.get_actor_label() if isinstance(obj, unreal.Actor) else None} if obj else None

def flush():
    (OUT / ('pie_after_z_fix.json' if VERIFY_Z else 'pie_results.json')).write_text(json.dumps(RESULT, ensure_ascii=False, indent=2), encoding='utf-8')

def components_at(mesh, transform, ignore):
    result = unreal.SystemLibrary.component_overlap_components(mesh, transform, TYPES, unreal.PrimitiveComponent, ignore)
    if result is None:
        return []
    if isinstance(result, tuple):
        result = result[-1]
    return [c for c in result if c.get_collision_response_to_channel(mesh.get_collision_object_type()) == BLOCK
            and mesh.get_collision_response_to_channel(c.get_collision_object_type()) == BLOCK]

def trace(world, start, end, ignore):
    hit = unreal.SystemLibrary.line_trace_single_for_objects(world, start, end, TYPES, False, ignore,
        unreal.DrawDebugTrace.NONE, True)
    if isinstance(hit, tuple):
        hit = hit[-1]
    if hit is None:
        return None
    parts = hit.to_tuple()
    if not parts[0]:
        return None
    return {'point': vec(parts[5]), 'normal': vec(parts[7]), 'actor': named(parts[9]),
            'component': named(parts[10]), 'initial_overlap': parts[1]}

def inspect_chest(world, point, chest):
    mesh = chest.get_editor_property('chest_mesh')
    transform = mesh.get_world_transform()
    overlaps = components_at(mesh, transform, [point, chest])
    raised = unreal.Transform(location=transform.translation + unreal.Vector(0, 0, 3),
        rotation=transform.rotation.rotator(), scale=transform.scale3d)
    raised_overlaps = components_at(mesh, raised, [point, chest])
    center, extent, radius = unreal.SystemLibrary.get_component_bounds(mesh)
    bottom = center.z - extent.z
    supports = []
    for dx, dy in [(0, 0), (-0.8, -0.8), (-0.8, 0.8), (0.8, -0.8), (0.8, 0.8)]:
        xy = unreal.Vector(center.x + dx * extent.x, center.y + dy * extent.y, bottom)
        supports.append(trace(world, xy + unreal.Vector(0, 0, 15), xy - unreal.Vector(0, 0, 150), [point, chest]))
    return {'location': vec(chest.get_actor_location()), 'mesh_center': vec(center), 'mesh_extent': vec(extent),
            'locked': chest.is_locked(), 'overlaps': [named(c) for c in overlaps],
            'overlaps_raised_3cm': [named(c) for c in raised_overlaps], 'supports': supports,
            'simulating_physics': mesh.is_simulating_physics(), 'class': chest.get_class().get_path_name()}

def collect(world):
    points = [a for a in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.ChestSpawnPoint)
              if a.get_actor_label().startswith('BP_ChestSpawnPoint_Land_Random_')]
    managers = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.GlobalLootSpawnManager)
    row = {'world': world.get_path_name(), 'time_seconds': unreal.GameplayStatics.get_time_seconds(world),
           'points': [], 'counts': [0, 0, 0, 0], 'manager_count': len(managers)}
    assert len(points) == (26 if VERIFY_Z else 27), len(points)
    for p in sorted(points, key=lambda a: int(a.get_actor_label().rsplit('_', 1)[1])):
        chest = p.get_spawned_actor()
        zone = ZONES.index(p.get_progression_zone())
        entry = {'label': p.get_actor_label(), 'zone': zone, 'activated': p.is_activated(),
                 'spawn_mode': str(p.get_spawn_mode()), 'kind': str(p.get_progression_kind()),
                 'environment': str(p.get_environment()), 'spawned': named(chest)}
        if chest:
            row['counts'][zone] += 1
            try:
                entry['inspection'] = inspect_chest(world, p, chest)
            except Exception:
                entry['inspection_error'] = traceback.format_exc()
        row['points'].append(entry)
    row['activated_counts'] = [sum(p['activated'] and p['zone'] == i for p in row['points']) for i in range(4)]
    row['counts_pass'] = row['counts'] == EXPECTED_COUNTS and row['activated_counts'] == EXPECTED_COUNTS
    row['seed'] = managers[0].get_editor_property('spawn_seed') if managers else None
    row['active_census'] = [managers[0].get_last_active_chest_count(z) for z in ZONES] if managers else []
    row['rebalance_pass'] = row['active_census'] == ([26, 22, 21, 32] if VERIFY_Z else [26, 22, 22, 32])
    RESULT['runs'].append(row)
    unreal.log('LAND_RANDOM_PIE_RUN %d counts=%s seed=%s' % (len(RESULT['runs']), row['counts'], row['seed']))
    flush()

class Runner:
    def __init__(self):
        self.state = 'start'
        self.deadline = time.monotonic() + 240
        self.next_time = time.monotonic() + 2
        self.frames = 0
        self.handle = unreal.register_slate_post_tick_callback(self.tick)

    def finish(self):
        flush()
        unreal.unregister_slate_post_tick_callback(self.handle)
        unreal.EditorPythonScripting.set_keep_python_script_alive(False)
        unreal.SystemLibrary.quit_editor()

    def tick(self, delta):
        try:
            now = time.monotonic()
            if now > self.deadline:
                raise RuntimeError('PIE stage timed out: ' + self.state)
            if now < self.next_time:
                return
            if self.state == 'start':
                LEVEL.editor_request_begin_play()
                self.state = 'playing'
                self.frames = 0
                self.next_time = now + 5
                self.deadline = now + 240
            elif self.state == 'playing':
                world = EDITOR.get_game_world()
                if not world or not LEVEL.is_in_play_in_editor():
                    return
                # Geometry audit only: keep initially spawned Actors until inspected.
                # Production pooling is separately captured in pie_pooling_enabled.json.
                for pool in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.StorageChestPool):
                    pool.set_actor_tick_enabled(False)
                self.frames += 1
                if self.frames < 30:
                    return
                if unreal.GameplayStatics.get_time_seconds(world) < 5:
                    return
                collect(world)
                LEVEL.editor_request_end_play()
                self.state = 'stopping'
                self.next_time = now + 2
            elif self.state == 'stopping' and not LEVEL.is_in_play_in_editor():
                if len(RESULT['runs']) >= (3 if VERIFY_Z else 12):
                    self.finish()
                else:
                    self.state = 'start'
                    self.next_time = now + 1
                    self.deadline = now + 240
        except Exception:
            RESULT['errors'].append(traceback.format_exc())
            unreal.log_error(RESULT['errors'][-1])
            self.finish()

try:
    # Validate reflected HitResult field access before starting a costly PIE session.
    assert len(unreal.HitResult().to_tuple()) == 18
    RESULT['pool_actor_tick_disabled_for_geometry_audit'] = True
    world = unreal.EditorLoadingAndSavingUtils.load_map('/Game/Level/Lvl_CY')
    assert world
    # Verify saved values from a fresh process before any PIE mutation.
    balance = unreal.load_asset('/Game/Blueprints/Item/Data/DA_ProgressionBalance')
    RESULT['saved_zone_plans'] = [p.export_text() for p in balance.get_editor_property('zone_plans')]
    RESULT['saved_modes'] = {a.get_actor_label(): str(a.get_spawn_mode())
        for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
        if isinstance(a, unreal.ChestSpawnPoint) and a.get_actor_label().startswith('BP_ChestSpawnPoint_Land_Random_')}
    flush()
    RUNNER = Runner()
except Exception:
    RESULT['errors'].append(traceback.format_exc())
    flush()
    unreal.SystemLibrary.quit_editor()
