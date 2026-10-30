"""Validate the last completed input-driven two-peer swim contact probe."""
import argparse
from datetime import datetime
import json
from pathlib import Path
import re
from statistics import median

p = argparse.ArgumentParser()
p.add_argument('--session', type=Path, required=True)
p.add_argument('--log', type=Path, required=True)
p.add_argument('--authored', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--cold', type=Path)
args = p.parse_args()
out = args.session / 'SwimContactsVerified'
assert (out / 'complete.json').exists(), 'Wait for the probe to finish'
load = lambda path: json.loads(path.read_text(encoding='utf-8'))
phase = None; start = None; events = []; catches = []; completed = False
for line in args.log.read_text(encoding='utf-8', errors='replace').splitlines():
    stamp = re.match(r'\[(\d{4}\.\d{2}\.\d{2}-\d{2}\.\d{2}\.\d{2}:\d{3})\]', line)
    if not stamp: continue
    time = datetime.strptime(stamp[1], '%Y.%m.%d-%H.%M.%S:%f')
    if 'SW_SWIM_NETWORK_PHASE ' in line:
        phase = line.split('SW_SWIM_NETWORK_PHASE ')[1].strip(); start = time
        if phase == 'settle': events = []; catches = []; completed = False
    if 'SW_SWIM_NETWORK_COMPLETE' in line: completed = True; phase = None
    if not phase: continue
    elapsed = (time - start).total_seconds()
    m = re.search(r'Submit .* NetMode=(\d+) Event=(5|6) Actor=(\S+)', line)
    if m: events.append({'phase': phase, 'mode': int(m[1]), 'event': int(m[2]), 'actor': m[3], 'elapsed': elapsed})
    m = re.search(r'SW_SWIM_CATCH .* NetMode=(\d+) Actor=(\S+) Side=(\w) Clip=(\S+) Time=([\d.]+) Played=(\d)', line)
    if m: catches.append({'phase': phase, 'mode': int(m[1]), 'actor': m[2], 'side': m[3], 'clip': m[4], 'world_time': float(m[5]), 'played': bool(int(m[6])), 'elapsed': elapsed})
assert completed
def count(label, event, settled=False):
    return [sum(x['phase'] == label and x['mode'] == mode and x['event'] == event and (not settled or x['elapsed'] > 1.5) for x in events) for mode in [2, 3]]
checks = {'surface_counts': {}, 'cadence_seconds': {}, 'settled_stop_counts': {}}
for label in ['host_forward', 'client_forward', 'host_left', 'host_back']:
    values = count(label, 5)
    assert all(n > 2 for n in values), (label, values)
    checks['surface_counts'][label] = values
    for mode in [2, 3]:
        rows = [x for x in catches if x['phase'] == label and x['mode'] == mode]
        assert len(rows) == values[mode - 2] and all(x['played'] for x in rows), (label, mode, 'extra distance-driven or dropped events')
        if label.endswith('forward'):
            assert all(a['side'] != b['side'] for a, b in zip(rows, rows[1:])), (label, mode, 'hands do not alternate')
            cadence = median(b['world_time'] - a['world_time'] for a, b in zip(rows, rows[1:]))
            assert .3 < cadence < .8
            checks['cadence_seconds'][label + '/NetMode' + str(mode)] = cadence
for label in ['idle', 'host_stop', 'client_stop', 'final_stop', 'underwater_stop']:
    values = [sum(count(label, event, True)[i] for event in [5, 6]) for i in [0, 1]]
    assert values == [0, 0], (label, values)
    checks['settled_stop_counts'][label] = values
checks['underwater_strokes'] = count('underwater_forward', 6)
assert all(n > 3 for n in checks['underwater_strokes'])
assert count('underwater_forward', 5) == [0, 0]
authored = load(args.authored)
assert len(authored) == 8 and sum(len(r['contacts']) for r in authored) == 16
assert all(r['pose_signature_preserved'] for r in authored)
waves = load(out / 'network_signal_summary.json')
assert all(r['clipped_samples'] == 0 for r in waves)
for phase in ['host_forward', 'client_forward', 'host_left', 'host_back', 'underwater_forward']:
    for i in [0, 1]:
        wave = next(r for r in waves if r['file'] == f'network_verified_swim_{phase}_device{i}.wav')
        assert wave['rms'] > 0
log = args.log.read_text(encoding='utf-8', errors='replace')
tests = re.findall(r'Test Completed\. Result=\{(\w+)\}.*?Path=\{(SW\.(?:Audio|Foley)\.[^}]+)\}', log)
assert len(tests) == 17 and all(status == 'Success' for status, _ in tests)
report = {'date': '2026-10-28', 'engine': '5.7.4', 'map': '/Game/Level/Lvl_CY',
          'authored_clips': 8, 'authored_markers': 16, 'checks': checks,
          'tests': [{'name': name, 'result': status} for status, name in tests],
          'recordings': waves, 'catch_clips': sorted({r['clip'] for r in catches}),
          'limitations': ['Surface hand catches follow the dominant replicated local pose, not a synchronized RPC clock.',
                          'Submerged travel keeps its existing distance cadence and a separate underwater cue.',
                          'The artificial editor-property play-rate trial was excluded; variable-rate linked-graph playback remains unverified.',
                          'No package/cook, packet-loss or late-join test; recorded output is not subjective timbre approval.']}
if args.cold:
    cold = load(args.cold)
    assert cold == {'clips': 8, 'markers': 16, 'saved_metadata_round_trip': True}
    report['saved_assets_cold_verification'] = cold
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
print(json.dumps(checks, ensure_ascii=False, indent=2))
