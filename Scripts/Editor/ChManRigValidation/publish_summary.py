"""Reduce completed native PIE/automation evidence to a durable bounded report."""
import json,re
from pathlib import Path
root=Path(__file__).resolve().parents[3]
saved=root/'Saved/ChManRigValidation'
run=json.loads((saved/'dry_marker_saved_connection.json').read_text(encoding='utf8'))
assert not run['errors'] and len(run['cases'])==3
assert run['controller_class'].endswith('BP_BasePlayerController_C')
audit=json.loads((saved/'final_asset_audit.json').read_text(encoding='utf8'))
assert audit['passed']
summary={'date':'2026-10-31','runtime':'real standalone PIE, production saved male BP/ABP and controller',
         'controller':run['controller_class'],'mesh':audit['mesh'],'animation':audit['anim_class'],
         'post_process':audit['post_process'],'errors':run['errors'],'cases':[]}
for case in run['cases']:
    assert case['phase_stats']['sprint']['max_speed']>600
    assert case['nonfinite']==0 and not case['outliers']
    assert case['max_scale_error']<.001 and case['max_limb_length_error_cm']<1
    row={k:v for k,v in case.items() if k not in ['animations','databases']}
    row['animations']=list(case['animations']);row['databases']=list(case['databases'])
    row['effective_sample_fps']=(case['frames']-1)/case['sampled_seconds']
    summary['cases'].append(row)
log=(saved/'automation_final.log').read_text(encoding='utf8',errors='replace')
summary['automation_successes']=re.findall(r'Test Completed\. Result=\{Success\} Name=\{([^}]+)\}',log)
summary['automation_failures']=re.findall(r'Test Completed\. Result=\{Fail[^}]*\} Name=\{([^}]+)\}',log)
assert len(summary['automation_successes'])==14 and not summary['automation_failures']
summary['limitations']=['standalone fixture, not full multiplayer game','weapon contact/all attack montages not visually approved',
                        'auto-fit physics, ragdoll not tuned','LOD/cook/packaging untested',
                        'source animation missing experimental DB/notify reference warnings remain',
                        '75 degree local rotation per-frame candidate threshold, not all-motion guarantee']
path=root/'docs/validation/ChMan_Runtime_20261031.json'
path.write_text(json.dumps(summary,ensure_ascii=False,indent=2),encoding='utf8')
print(json.dumps({'frames':sum(c['frames'] for c in summary['cases']),
                  'tests':len(summary['automation_successes']),
                  'cases':[{k:c[k] for k in ['name','effective_sample_fps','nonfinite','max_deform_rotation_step_deg','max_limb_length_error_cm','helper_errors','helper_rotation_errors','databases']} for c in summary['cases']]},indent=2))
