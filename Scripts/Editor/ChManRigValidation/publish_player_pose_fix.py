"""Publish evidence from the real integrated project, not the earlier private copy."""
import json,shutil
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3];SAVED=ROOT/'Saved/PlayerRagdollPoseFix'
read=lambda path:json.loads((SAVED/path).read_text(encoding='utf-8-sig'))
runtime=read('player_pose_fix.json');life=read('playground_test.json');automation=read('Automation/index.json');applied=read('applied.json');setup=read('playground_setup.json')
assert runtime['completed'] and not runtime['errors'] and len(runtime['cases'])==8
assert life['passed'] and not life['errors'] and all(9.8<e['seconds']<10.5 for e in life['events'] if e['event']=='respawn')
assert automation['failed']==0 and automation['notRun']==0 and automation['succeeded']+automation['succeededWithWarnings']==23
assert applied['passed'] and setup['passed'] and not setup['keys']['conflicts']
rows=[];images=[];image_dir=ROOT/'docs/images/PlayerRagdollPoseFix';image_dir.mkdir(parents=True,exist_ok=True)
for case in runtime['cases']:
    assert case['nonfinite']==0 and case['restored_nonfinite']==0 and case['restored_not_simulating'] and case['restored_anim_unpaused'] and case['restored_speed']>200
    assert case['max_visual_physical_error_cm']<.01 and case['corrected_twist_off_axis_cm']<.01
    assert case['material_slots']['source']==case['material_slots']['visual'] and all(case['material_slots']['visual'])
    row={k:case[k] for k in ['character','mode','frames','effective_fps','max_limit_excess_deg','max_anchor_error_cm','max_visual_physical_error_cm','source_twist_off_axis_cm','corrected_twist_off_axis_cm','restored_speed','material_slots','worst_joint']}
    row['final_limit_excess_deg']=max(j['angular_excess_deg'] for j in case['snapshots'][-1]['joints']);rows.append(row)
    for shot in case['screenshots']:
        source=Path(shot['file']);assert source.is_file();dest=image_dir/source.name;shutil.copy2(source,dest);images.append(dest.relative_to(ROOT).as_posix())
assert len(images)==20
for c in applied['characters']:assert '/AngularLimitsReview/' not in c['after']['asset']
result={'date':'2026-10-31','passed':True,'scope':'actual project integration; no external plugin/test overlay required',
 'automatic_install':'SWPlayerRagdollComponent::StartRagdoll -> SWCorpsePoseLibrary::Install',
 'test_map':setup['map'],'test_keys':setup['keys'],'frames':sum(c['frames'] for c in rows),'cases':rows,
 'lifecycle':life['events'],'automation':{'passed':23,'with_warnings':automation['succeededWithWarnings'],'tests':[{'path':t['fullTestPath'],'state':t['state']} for t in automation['tests']]},
 'physics_assets':[c['after']['asset'] for c in applied['characters']],'screenshots':images,
 'limits':['Finite solver still has transient angular excess; not a zero-error hard clamp','Cloth/hair/skin penetration is not fully solved','LOD/morph/custom primitive data, dedicated server, packet latency, mass-corpse performance and cook/packaging not verified in this change','Test GameMode is single-player PIE only; production 5-second lifecycle retained']}
dest=ROOT/'docs/validation/Player_Ragdoll_PoseFix_20261031.json';dest.write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
print(json.dumps({'passed':True,'cases':len(rows),'frames':result['frames'],'respawn_seconds':[e['seconds'] for e in life['events'] if e['event']=='respawn'],'automation':23,'images':len(images)},ensure_ascii=False,indent=2))
