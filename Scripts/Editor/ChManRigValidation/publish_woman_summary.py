import json,re,hashlib
from pathlib import Path
root=Path(__file__).resolve().parents[3];saved=root/'Saved/ChWomanRigValidation';art=root/'SourceArt/Characters/ChWoman'
run=json.loads((saved/'final_saved_connection.json').read_text(encoding='utf8'));assets=json.loads((saved/'final_asset_audit.json').read_text(encoding='utf8'))
assert not run['errors'] and len(run['cases'])==3 and assets['passed']
report={'date':'2026-10-31','runtime':'real standalone PIE, saved production female BP/ABP, production controller, no runtime mesh/ABP override',
        'mesh':assets['mesh'],'animation':assets['anim_class'],'post_process':assets['post_process'],'controller':run['controller_class'],'cases':[]}
for c in run['cases']:
    assert c['nonfinite']==0 and not c['outliers'] and c['max_limb_length_error_cm']<1
    # Unweighted markers keep authored non-unit scale. Source clips also animate twist scale.
    assert c['max_deform_scale_error']<.01
    assert c['phase_stats']['sprint']['max_speed']>600 and c['phase_stats']['jump_forward']['air_frames']>10
    assert all(p['invalid_active_mm_frames']==0 for n,p in c['phase_stats'].items() if n!='idle')
    assert max(c['helper_errors'].values())<.001 and max(c['helper_rotation_errors'].values())<.001
    row={k:v for k,v in c.items() if k not in ['databases','animations']};row['databases']=list(c['databases']);row['animations']=list(c['animations']);row['effective_fps']=(c['frames']-1)/c['sampled_seconds'];report['cases'].append(row)
for label,path in [('controls',art/'Work/control_validation.json'),('bake_blender',art/'Work/animation_bake_roundtrip_audit.json'),('bake_unreal',saved/'native_bake_audit.json'),('ue_roundtrip',art/'Work/ue_roundtrip_audit.json'),('eye_binding',art/'Work/eye_binding_audit.json'),('source_scale',saved/'source_scale_audit.json'),('socket_reimport',saved/'socket_reimport_audit.json')]:report[label]=json.loads(path.read_text(encoding='utf8'))
assert report['controls']['passed'] and report['bake_unreal']['passed']
assert report['socket_reimport']['passed'] and len(assets['mesh_description']['sockets'])==13
report['ue_eye_weights']=json.loads((art/'Work/ue_eye_weight_audit.json').read_text(encoding='utf8'));assert report['ue_eye_weights']['passed']
log=(saved/'automation.log').read_text(encoding='utf8',errors='replace');report['automation_successes']=re.findall(r'Test Completed\. Result=\{Success\} Name=\{([^}]+)\}',log);report['automation_failures']=re.findall(r'Test Completed\. Result=\{Fail[^}]*\} Name=\{([^}]+)\}',log)
assert len(report['automation_successes'])==14 and not report['automation_failures']
report['fbx_sha256']=hashlib.sha256((art/'FBX/SKM_ChWoman_UE5.fbx').read_bytes()).hexdigest().upper()
report['limitations']=['standalone fixture, full multiplayer game untested','weapon contact/all attack montages not visually approved','auto-fit physics, ragdoll not tuned','LOD/cook/packaging untested','jaw/lips/blinks/morph facial rig not authored','custom authoring rig, not full Rigify/UE Control Rig implementation','source animation missing experimental DB/notify reference warnings remain','75 degree local non-IK rotation candidate threshold, not an all-motion guarantee','non-unit unweighted helper scale preserved; twist scale varies up to 0.679 percent in runtime']
path=root/'docs/validation/ChWoman_Runtime_20261031.json';path.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
print(json.dumps({'frames':sum(c['frames'] for c in report['cases']),'tests':len(report['automation_successes']),'fbx_sha256':report['fbx_sha256']},indent=2))
