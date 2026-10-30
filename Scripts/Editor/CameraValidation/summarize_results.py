"""Summarize bounded two-player PIE evidence; run with the live output directory."""
import collections
import json
import math
from pathlib import Path
import statistics
import sys

root = Path(sys.argv[1])
read = lambda name: json.loads((root / name).read_text(encoding='utf-8-sig'))
camera = read('camera_network_samples.json')
swim = read('camera_swim_samples.json')
geometry = read('geometry_validation.json')
groups = collections.defaultdict(list)
for row in camera:
    for peer, world in enumerate(row['worlds']):
        groups[(row['phase'], peer)].append(world)
camera_summary = []
for (phase, peer), values in groups.items():
    camera_summary.append({'phase': phase, 'peer': peer, 'samples': len(values),
        'mean_distance_cm': statistics.mean(v['distance'] for v in values),
        'min_distance_cm': min(v['distance'] for v in values),
        'max_distance_cm': max(v['distance'] for v in values),
        'blocking_components': sorted({v['blocker'] for v in values if v['blocker']}),
        'ignored_counts': sorted({v['ignored'] for v in values})})
groups = collections.defaultdict(list)
disagreements = 0
max_position_delta = 0.
zero_camera_samples = []
for row in swim:
    if row['elapsed'] <= 2: continue
    for peer, world in enumerate(row['worlds']):
        groups[(row['phase'], peer)].append(world)
        if world['camera_distance'] < 1:
            zero_camera_samples.append({'phase': row['phase'], 'peer': peer, 'time': row['time']})
    local = row['worlds'][1]
    authority = next(p for p in row['server_players'] if p['name'] == local['owner'])
    disagreements += int(any(local[k] != authority[k] for k in ['swimming', 'underwater']))
    max_position_delta = max(max_position_delta, math.dist(local['location'], authority['location']))
swim_summary = []
for (phase, peer), values in groups.items():
    swim_summary.append({'phase': phase, 'peer': peer, 'samples': len(values),
        'swimming': sorted({v['swimming'] for v in values}),
        'underwater': sorted({v['underwater'] for v in values}),
        'camera_lag': sorted({v['camera_lag'] for v in values}),
        'movement_modes': sorted({v['movement_mode'] for v in values}),
        'min_camera_distance_cm': min(v['camera_distance'] for v in values),
        'max_camera_distance_cm': max(v['camera_distance'] for v in values)})
result = {'schema_version': 1, 'date': '2026-10-30', 'map': '/Game/Level/Lvl_CY',
    'method': 'Two-player single-process listen server/client PIE; parked camera comparisons on live ship; injected movement/dive/ascend input with teleport setup between cabin and water.',
    'authoring': read('author_result.json'), 'camera': camera_summary, 'swimming': swim_summary,
    'geometry': geometry, 'swim_state_disagreements_after_2s': disagreements,
    'max_client_server_position_delta_cm_after_2s': max_position_delta,
    'near_zero_camera_samples_after_2s': zero_camera_samples,
    'sweep_budget_exhaustions': sum(w['budget'] for row in camera + swim for w in row['worlds']),
    'raw_output_directory': str(root)}
if (root / 'return_diagnostic.json').exists():
    diagnostic = read('return_diagnostic.json')
    result['return_diagnostic'] = []
    for policy in [False, True]:
        for peer in [0, 1]:
            values = [r['worlds'][peer] for r in diagnostic if r['policy'] == policy and (r['elapsed'] % 6) > 2]
            result['return_diagnostic'].append({'policy': policy, 'peer': peer,
                'min_distance_cm': min(v['distance'] for v in values), 'max_distance_cm': max(v['distance'] for v in values),
                'blockers': sorted({v['blocker'] for v in values if v['blocker']})})
output = Path('docs/validation/Ship_Camera_Multiplayer_20261030.json')
output.write_text(json.dumps(result, indent=2), encoding='utf-8')
print(json.dumps({'output': str(output), 'camera_samples': len(camera), 'swim_samples': len(swim),
    'state_disagreements': disagreements, 'budget_exhaustions': result['sweep_budget_exhaustions'],
    'near_zero_camera_samples': len(zero_camera_samples)}, indent=2))
