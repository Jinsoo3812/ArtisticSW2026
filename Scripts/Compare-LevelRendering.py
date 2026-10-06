"""Compare analyzed export directories and reject mismatched rendering conditions.

Usage: python Scripts/Compare-LevelRendering.py <baseline-dir> <candidate-dir> ...
Run Analyze-LevelRendering.py for each directory first.
"""
import json
import sys
from pathlib import Path

if len(sys.argv) < 3:
    raise SystemExit(__doc__)

rows = []
baseline_conditions = None
variant_cvars = {'r.EarlyZPass', 'r.EarlyZPassOnlyMaterialMasking', 'r.VelocityOutputPass'}
for argument in sys.argv[1:]:
    root = Path(argument)
    metadata = json.loads((root / 'metadata.json').read_text(encoding='utf-8-sig'))
    summary = json.loads((root / 'summary.json').read_text(encoding='utf-8-sig'))
    if not metadata.get('stopped_own_trace'):
        raise SystemExit(f'{root}: incomplete trace')
    if metadata.get('shader_jobs_start', 0) or metadata.get('shader_jobs_end', 0):
        raise SystemExit(f'{root}: shaders were compiling during capture')
    for field in ['camera', 'render_state']:
        if field + '_start' not in metadata or metadata[field + '_start'] != metadata.get(field + '_end'):
            raise SystemExit(f'{root}: {field} changed during capture or was not recorded')
    if not metadata['render_state_start'].get('directional_lights'):
        raise SystemExit(f'{root}: no recorded directional light')
    single_scenes = summary['independent_scenes_per_frame'].get('1', 0)
    # Timestamp attribution at GPU frame boundaries can assign one transient
    # scene to its neighbour. Reject sustained multi-view rendering (>1%).
    if single_scenes / summary['central_frames'] < 0.99:
        raise SystemExit(f'{root}: sustained missing or multiple scene renders per graphics frame')
    conditions = {key: metadata[key] for key in ['engine', 'map', 'net_mode', 'cpu', 'gpu',
                  'viewport_width', 'viewport_height', 'camera_start', 'render_state_start', 'landscapes']}
    conditions['cvars'] = {key: value for key, value in metadata['cvars'].items() if key not in variant_cvars}
    if baseline_conditions is None:
        baseline_conditions = conditions
    elif conditions != baseline_conditions:
        differences = [key for key in conditions if conditions[key] != baseline_conditions[key]]
        raise SystemExit(f'{root}: incomparable conditions: {", ".join(differences)}')
    rows.append((metadata.get('render_profile', root.name), summary))

print('Profile | Depth | Base | Velocity | Shadow | Scene mean / p95 (ms)')
for profile, summary in rows:
    passes = summary['passes']
    means = [passes[name]['mean_ms'] for name in ['PrePass %s %s', 'BasePass', 'RenderVelocities(%s)', 'ShadowDepths']]
    scene = passes['SceneRender']
    print(f'{profile} | ' + ' | '.join(f'{value:.3f}' for value in means)
          + f" | {scene['mean_ms']:.3f} / {scene['p95_ms']:.3f}")
baseline = rows[0][1]['passes']['SceneRender']['mean_ms']
for profile, summary in rows[1:]:
    value = summary['passes']['SceneRender']['mean_ms']
    print(f'{profile}: SceneRender {(value / baseline - 1) * 100:+.1f}% vs baseline')
print('Graphics scopes include nested children; async queues and frame rate are not added to these timings.')
