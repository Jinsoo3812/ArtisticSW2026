"""Combine completed network audio probes; retain setup failures in their source logs."""
import argparse
from collections import Counter
import json
from pathlib import Path
import re

p = argparse.ArgumentParser()
p.add_argument('--session', type=Path, required=True)
p.add_argument('--log', type=Path, required=True)
p.add_argument('--recheck-session', type=Path, required=True)
p.add_argument('--recheck-log', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
args = p.parse_args()


def events(path, marker, complete):
    phase = None
    result = []
    for line in path.read_text(encoding='utf-8', errors='replace').splitlines():
        if marker in line:
            phase = line.split(marker, 1)[1].strip()
            if phase == 'npc_entry':
                result = []  # Use the last complete run when a fixture was corrected.
        m = re.search(r'NetMode=(\d+) Event=(\d+) Actor=(\S+)', line)
        if m and phase:
            mode, event, actor = m.groups()
            result.append({'phase': phase, 'mode': int(mode), 'event': int(event), 'actor': actor})
        if complete in line:
            phase = None
    return result


def counts(rows, phase, event):
    c = Counter(r['mode'] for r in rows if r['phase'] == phase and r['event'] == event)
    return [c[2], c[3]]


load = lambda path: json.loads(path.read_text(encoding='utf-8'))
main = args.session / 'CoverageVerified'
recheck = args.recheck_session / 'CoverageRecheckFinal'
assert (main / 'coverage_complete.json').exists()
assert (recheck / 'complete.json').exists()
samples = load(main / 'coverage_samples.json')
retake = load(recheck / 'samples.json')
main_events = events(args.log, 'SW_AUDIO_COVERAGE_PHASE ', 'SW_AUDIO_COVERAGE_COMPLETE')
new_events = events(args.recheck_log, 'SW_AUDIO_COVERAGE_RECHECK_PHASE ', 'SW_AUDIO_COVERAGE_RECHECK_COMPLETE')

checks = {}
checks['stationary_surface_strokes'] = counts(main_events, 'player_float', 5)
checks['forward_swim_strokes'] = counts(main_events, 'player_swim', 5)
assert checks['stationary_surface_strokes'] == [0, 0]
assert all(n > 0 for n in checks['forward_swim_strokes'])
checks['npc_full_entries'] = counts(new_events, 'npc_entry', 3)
assert checks['npc_full_entries'] == [1, 1]
checks['npc_replica_names'] = load(recheck / 'complete.json')
assert len(checks['npc_replica_names']) == 2

checks['leak_voices'] = {}
for phase, expected in [('leak_player', 1), ('leak_enemy', 1), ('leak_many', 3), ('leak_off', 0),
                        ('leak_far', 0), ('leak_return', 1), ('leak_hidden', 0),
                        ('leak_resume', 1), ('leak_final_off', 0)]:
    last = next(r for r in reversed(samples) if r['phase'] == phase)
    voices = [len(re.findall(r'Leak=\S+ Playing=1', w['ship_audio'])) for w in last['worlds']]
    assert voices == [expected, expected], (phase, voices)
    checks['leak_voices'][phase] = voices

checks['cabin_states_after_settle'] = {}
for phase, expected in [('rain_out', False), ('rain_cabin', True), ('rain_out_again', False)]:
    rows = [r for r in retake if r['phase'] == phase and r['elapsed'] > 2]
    assert rows and all(w['cabin'] == expected for r in rows for w in r['worlds'])
    checks['cabin_states_after_settle'][phase] = [expected, expected]

checks['gull_day_calls'] = counts(main_events, 'birds_day', 16)
checks['gull_night_calls_observed_12s'] = counts(main_events, 'birds_night', 16)
assert all(n > 0 for n in checks['gull_day_calls'])
assert checks['gull_night_calls_observed_12s'] == [0, 0]
night = next(r for r in reversed(samples) if r['phase'] == 'birds_night')
assert all(not w['wildlife_allowed'] for w in night['worlds'])

waves = load(recheck / 'network_signal_summary.json')
assert len(waves) == 8 and all(w['rms'] > 0 and w['clipped_samples'] == 0 for w in waves)
checks['rain_cabin_to_outside_rms_ratio'] = []
for device in [0, 1]:
    rms = lambda phase: next(w['rms'] for w in waves if w['file'] == f'network_verified_recheck_{phase}_device{device}.wav')
    ratio = rms('rain_cabin') / rms('rain_out')
    assert ratio < .4 and .7 < rms('rain_out_again') / rms('rain_out') < 1.3
    checks['rain_cabin_to_outside_rms_ratio'].append(ratio)

test_log = args.log.read_text(encoding='utf-8', errors='replace')
tests = re.findall(r'Test Completed\. Result=\{(\w+)\}.*?Path=\{(SW\.(?:Audio|Foley)\.[^}]+)\}', test_log)
assert len(tests) == 15 and all(status == 'Success' for status, _ in tests)
report = {'checks': checks, 'tests': tests, 'recheck_waveforms': waves,
          'evidence': {k: str(v) for k, v in vars(args).items() if k != 'output'},
          'limits': ['Master recordings prove output, not subjective timbre approval.',
                     'Final NPC master also contains rain; initial isolated NPC recordings are retained in CoverageRecheck.',
                     'Cabin mix retake fixes ship transforms to isolate the occupied voxel boundary from physics jitter.',
                     'Only live falling NPCs are handled; corpses/ragdolls need separate impact integration.',
                     'Night observation was 12 seconds; late join, packet loss, all enemy variants and packaged play remain untested.']}
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
print(json.dumps(checks, ensure_ascii=False, indent=2))
