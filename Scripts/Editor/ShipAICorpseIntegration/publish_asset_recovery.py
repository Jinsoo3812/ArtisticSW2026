"""Publish verified recovery evidence; never edits Unreal assets."""
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / 'Saved/AssetRepair'

def read(name):
    return json.loads((OUT / name).read_text(encoding='utf-8-sig'))

files = read('package_file_audit.json')
assets = read('engine_asset_audit.json')
automation = read('Automation/index.json')
motion = read('motion_matching.json')
fixture = read('fixture_write_verification.json')
assert files['passed'] and assets['passed'] and fixture['passed']
assert not assets['missing_hard_references'] and not assets['load_failures']
assert automation['failed'] == automation['notRun'] == 0
assert automation['succeeded'] + automation['succeededWithWarnings'] == 62
assert len(motion['cases']) == 2 and not motion['errors']
for case in motion['cases']:
    assert case['nonfinite'] == 0 and not case['outliers']
    assert case['max_limb_length_error_cm'] < 1 and case['max_deform_scale_error'] < .01
    assert len(case['phase_stats']) == 12
    assert all(p['invalid_active_mm_frames'] == 0 for name, p in case['phase_stats'].items() if name != 'idle')
    assert case['phase_stats']['sprint']['max_speed'] > 600
    assert case['phase_stats']['jump_forward']['air_frames'] > 5
log = (OUT / 'CompileBlueprints.log').read_text(encoding='utf8')
counts = re.search(r'Compiling Completed with (\d+) errors and (\d+) warnings and (\d+) blueprints that failed to load', log)
assert counts and int(counts[1]) == int(counts[3]) == 0
assert 'DeckNavigationComponent' not in log
blueprints = {'total': log.count('Loading and Compiling:'),
              'project': len(re.findall(r'Loading and Compiling: ./Game/', log)),
              'errors': int(counts[1]), 'warnings': int(counts[2]), 'failed_loads': int(counts[3])}
result = {
    'branch': 'JYU_merge', 'baseline': 'd64d6ace256a6e5ddaf386f3c4c0c15a24edd745',
    'incoming_shipai_commit': '2a4fe193edbf30e201151ebc66151ea5d8d5623d',
    'initial_missing_reference_edges': 389,
    'file_audit': files, 'engine_asset_audit': assets, 'blueprint_compile': blueprints,
    'automation': {key: automation[key] for key in ['succeeded', 'succeededWithWarnings', 'failed', 'notRun', 'reportCreatedOn']},
    'automation_tests': [{'path': t['fullTestPath'], 'state': t['state']} for t in automation['tests']],
    'motion_matching': {key: value for key, value in motion.items() if key != 'phase_snapshots'},
    'motion_snapshot_evidence': 'Saved/AssetRepair/motion_matching.json (full bone snapshots kept locally)',
    'fixture_write_verification': fixture,
    'repairs': {name: read(name + '.json') for name in [
        'npc_repair', 'redirect_repair', 'optional_demo_repair', 'reference_normalization',
        'legacy_ship_blueprint_repair', 'original_sample_restoration', 'optional_state_machine_sources']},
    'world_smoke': read('world_smoke.json'), 'playground': read('playground_test.json'),
    'input_audit': read('input_audit.json'),
    'history_and_archive': {
        'initial_missing_targets': 52, 'historical_originals_found': 31,
        'history': 'SourceArt/AssetRecovery/MissingAssetHistory.json',
        'restoration': 'SourceArt/AssetRecovery/HistoricalRestoration.json',
        'archive_manifest': 'SourceArt/Legacy/OrphanWorlds/ArchiveManifest.json',
        'archive_packages': 215, 'archive_kind': 'structural preservation; original owning maps not recovered'},
    'respawn': {'production_seconds': 10, 'introduced_commit': '97f323805',
                'production_code_changed': False, 'editor_fixture': 'separate timer using existing production constant'},
    'scope': 'No production player, respawn, ship navigation, GAS or Config edits in this recovery; editor tooling, assets and invalid test fixtures repaired.',
    'limitations': [
        'Two missing demo art references use explicitly recorded current/engine defaults, not recovered original art.',
        'Orphan world packages preserved outside mounted Content; missing original maps remain unavailable.',
        'Demo baked lighting needs rebuilding; original BuiltData was never tracked.',
        'Eight unused vendor example Blueprint warnings remain.',
        'Engine Korean-locale FStructuredLogFormatTest has four startup checks failing; same map with culture=en passes. Engine/config unchanged.',
        'Cook/packaging, long network delay, mass corpse performance and every LOD/morph/material combination untested.'
    ]
}
assert result['playground']['passed'] and not result['input_audit']['conflicts']
target = ROOT / 'docs/validation/Asset_Recovery_20261101.json'
target.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf8')
print(json.dumps({'report': str(target), 'blueprints': blueprints, 'motion_frames': sum(c['frames'] for c in motion['cases'])}, ensure_ascii=False))
