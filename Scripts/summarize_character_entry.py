"""Record bounded evidence for the character-only water-entry sound revision."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import numpy as np
from prepare_audio_wavs import decode

p = argparse.ArgumentParser()
p.add_argument('--session', type=Path, required=True)
p.add_argument('--log', type=Path, required=True)
p.add_argument('--staging', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--cold-log', type=Path)
args = p.parse_args()
folder = args.staging / 'CharacterEntryWeight'
load = lambda file: json.loads(file.read_text(encoding='utf-8'))
entries = args.session / 'CharacterEntryWeightVerified/EntriesVerified'
samples = load(entries / 'network_character_entries.json')
names = list(dict.fromkeys(row['phase'] for row in samples))
assert len(names) == 2
phase = None; counts = Counter(); finished = False
log = args.log.read_text(encoding='utf-8', errors='replace')
for line in log.splitlines():
    if 'SW_NETWORK_ENTRY_PHASE ' in line:
        phase = line.split('SW_NETWORK_ENTRY_PHASE ')[1].strip()
        if phase == names[0]: counts.clear(); finished = False
    m = re.search(r'NetMode=(2|3) Event=3 Actor=(\S+)', line)
    if m and phase and m[2] in names: counts[m[2] + '/NetMode' + m[1]] += 1
    if 'SW_NETWORK_ENTRIES_DONE' in line: finished = True; phase = None
expected = {name + '/NetMode' + str(mode): 1 for name in names for mode in [2, 3]}
assert finished and dict(counts) == expected, counts
waves = load(entries / 'network_signal_summary.json')
assert len(waves) == 4 and all(row['rms'] > 0 and row['clipped_samples'] == 0 for row in waves)
authored = load(folder / 'authored.json')
pcm = []
for name in authored['waves']:
    a, rate = decode((args.staging / 'ShipWaterPrepared' / (name + '.wav')).read_bytes())
    b, rate2 = decode((folder / (name + '_exported.wav')).read_bytes())
    assert rate == rate2 and a.shape == b.shape
    error = float(abs(a - b).max()) * 32768
    assert error <= 1.0001, (name, error)
    pcm.append({'wave': name, 'frames': len(a), 'pcm_max_error_lsb': error, 'peak': float(abs(a).max())})
for name, expected_hash in load(folder / 'preserved_before.json').items():
    assert hashlib.sha256(Path(name).read_bytes()).hexdigest() == expected_hash, name
tests = re.findall(r'Test Completed\. Result=\{(\w+)\}.*?Path=\{(SW\.(?:Audio|Foley)\.[^}]+)\}', log)
assert len(tests) == 17 and all(status == 'Success' for status, _ in tests)
report = {'date': '2026-10-28', 'engine': '5.7.4', 'profile_volume_before': .55, 'profile_volume_after': .72,
          'full_entries': dict(counts), 'recordings': waves, 'imported_pcm': pcm,
          'preserved_cannon_foley_swim_hashes': True,
          'automation': [{'name': name, 'result': status} for status, name in tests],
          'scope': 'Controlled live falls of both players in two-peer PIE; not a subjective timbre approval or all environmental mixes.'}
if args.cold_log:
    cold = args.cold_log.read_text(encoding='utf-8', errors='replace')
    assert 'SW_CHARACTER_ENTRY_WEIGHT_VERIFIED' in cold and 'LogExit: Exiting.' in cold
    report['saved_assets_cold_reloaded'] = True
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
print(json.dumps({'full_entries': dict(counts), 'peak': max(r['peak'] for r in waves), 'clipped_samples': 0}))
