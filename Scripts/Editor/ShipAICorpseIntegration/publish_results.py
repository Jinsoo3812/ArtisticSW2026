"""Reduce real merged-project evidence without rewriting unrelated assets."""
import json,re,shutil,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3];OUT=ROOT/'Saved/ShipAICorpseIntegration'
def read(name):return json.loads((OUT/name).read_text(encoding='utf-8-sig'))
def git(*args):return subprocess.check_output(['git',*args],cwd=ROOT).decode('utf8')
files=read('package_file_audit.json');engine=read('engine_asset_audit.json');rig=read('rig_migration.json')
corpse=read('corpse_visuals.json');network=read('network.json');motion=read('motion_matching.json');playground=read('playground_test.json');automation=read('Automation/index.json')
assert files['passed'] and engine['passed'] and rig['passed']
assert corpse['completed'] and len(corpse['cases'])==10 and not corpse['errors']
assert network['passed'] and not network['errors'] and len(network['samples'])>0
assert playground['passed'] and not playground['errors']
assert len(motion['cases'])==2 and not motion['errors']
assert automation['failed']==0 and automation['notRun']==0 and automation['succeeded']+automation['succeededWithWarnings']==23
for c in corpse['cases']:
    assert c['nonfinite']==0 and c['restored_nonfinite']==0 and c['restored_not_simulating'] and c['restored_anim_unpaused'] and c['restored_speed']>200
    assert c['max_visual_physical_error_cm']<.01 and c['corrected_twist_off_axis_cm']<.01
    assert c['material_slots']['source']==c['material_slots']['visual'] and all(c['material_slots']['visual'])
for c in motion['cases']:
    # Same deformation gate as the published female rig validation. Existing
    # unweighted IK markers have authored scales, so all-bone unit scale is invalid.
    assert c['nonfinite']==0 and not c['outliers'] and c['max_limb_length_error_cm']<1 and c['max_deform_scale_error']<.01
    assert len(c['phase_stats'])==12
    assert all(p['invalid_active_mm_frames']==0 for name,p in c['phase_stats'].items() if name!='idle')
    assert c['phase_stats']['sprint']['max_speed']>600 and c['phase_stats']['jump_forward']['air_frames']>5
delta=set(git('-c','core.quotepath=false','diff','--name-only','origin/KKH/ShipAI','--','Content').splitlines())
missing=engine['missing_hard_references']
for row in missing:
    row['source_changed_from_shipai']=any('Content/'+row['from'].removeprefix('/Game/')+ext in delta for ext in ['.umap','.uasset'])
assert not any(row['source_changed_from_shipai'] for row in missing)
log=(OUT/'CompileBlueprints.log').read_text(encoding='utf8')
counts=re.search(r'Compiling Completed with (\d+) errors and (\d+) warnings and (\d+) blueprints that failed to load',log)
assert counts
compile_summary={'compiled_total':log.count('Loading and Compiling:'),'compiled_project':len(re.findall(r'Loading and Compiling: ./Game/',log)),
                 'errors':int(counts[1]),'warnings':int(counts[2]),'failed_loads':int(counts[3]),'all_passed':int(counts[1])==0,
                 'known_error':'/Game/Chae_Asset/YiSunSinContent/Animations/ABP_YiSunSin has no target skeleton',
                 'other_load_errors':['BS_YiSunSin_Locomotion: AS_YiSunSin_Walk and AS_YiSunSin_Run sample bounds'],
                 'scope':'audit only; existing ShipAI NPC/sample assets not saved or altered'}
assert compile_summary['errors']==1 and compile_summary['failed_loads']==0
images=[]
for label in ['Man','Woman']:
    for mode in ['idle','platform']:
        name=label+'_'+mode+'_settled_angled.png';source=OUT/'Screenshots'/name
        dest=ROOT/'docs/images/PlayerRagdollPoseFix'/('ShipAI_'+name)
        assert source.is_file();shutil.copy2(source,dest);images.append(dest.relative_to(ROOT).as_posix())
result={'incoming_branch':'KKH/ShipAI','incoming_commit':git('rev-parse','origin/KKH/ShipAI').strip(),'destination_branch':'JYU_merge','before_merge_commit':'1c41c6e09d56a07e5fb9a77875a5c627b4f9fdf5',
 'test_keys':{'death':'Ctrl+Alt+Shift+F10','gender':'Ctrl+Alt+Shift+F11'},
 'runtime_content_validation_base':'180f2036; final ShipAI update 2a4fe193 changes only ship/enemy C++ and is checked by the final extended automation run',
 'policy':'ShipAI wins conflicts; preserve montage and based movement until DeathFinished; then corrected physical corpse',
 'player_corpse_regression_passed':True,'whole_project_asset_validation_passed':False,
 'file_audit':files,'engine_changed_asset_loads':len(engine['loaded_changed']),'engine_load_failures':engine['load_failures'],
 'blueprint_compile':compile_summary,'missing_hard_reference_candidates':missing,'rig_migration':rig,
 'automation':{'passed':23,'with_warnings':automation['succeededWithWarnings'],'tests':[{'path':t['fullTestPath'],'state':t['state']} for t in automation['tests']]},
 'corpse':{'cases':len(corpse['cases']),'frames':sum(c['frames'] for c in corpse['cases']),
 'rows':[{k:c.get(k) for k in ['character','mode','frames','nonfinite','restored_nonfinite','restored_speed','max_visual_physical_error_cm','corrected_twist_off_axis_cm','max_limit_excess_deg','max_anchor_error_cm','initial_base_momentum']} for c in corpse['cases']]},
 'network':{'passed':True,'samples':len(network['samples']),'new_lives':network['new_lives']},
 'motion_matching':[{k:c[k] for k in ['name','frames','nonfinite','outliers','max_scale_error','max_scale_detail','max_deform_scale_error','max_limb_length_error_cm','phase_stats','databases']} for c in motion['cases']],
 'playground':playground,'screenshots':images,
 'untested':['dedicated server','mass corpse performance','water and sinking','cook and packaging','all LOD/morph/custom material combinations'],
 'limitations':['transient solver limit excess persists','cloth/hair/skin/bag penetration not eliminated','initial idle MM warmup includes 1 male and 2 female invalid frames; subsequent active phases valid']}
final_report=OUT/'FinalAutomation/index.json'
if final_report.exists():
    final=json.loads(final_report.read_text(encoding='utf-8-sig'))
    failed=[t for t in final['tests'] if t['state']=='Fail']
    assert {t['fullTestPath'] for t in failed}=={'ArtisticSW.Enemy.Ship.Anchor.Integration','ArtisticSW.Enemy.Ship.Navigation.DeterministicAvoidance'}
    assert final['notRun']==0
    result['final_automation']={'passed':final['succeeded']+final['succeededWithWarnings'],'with_warnings':final['succeededWithWarnings'],
        'failed':final['failed'],'all_passed':False,'failure_details':failed,
        'tests':[{'path':t['fullTestPath'],'state':t['state']} for t in final['tests']]}
    result['extended_regression_all_passed']=False
input_audit=OUT/'input_audit.json'
if input_audit.exists():
    result['input_audit']=json.loads(input_audit.read_text(encoding='utf8'));assert not result['input_audit']['conflicts']
isolated=OUT/'ShipFailuresOnly/index.json'
if isolated.exists():
    isolated_data=json.loads(isolated.read_text(encoding='utf-8-sig'))
    result['isolated_ship_test_failures']={'failed':isolated_data['failed'],'tests':[t for t in isolated_data['tests'] if t['state']=='Fail']}
dest=ROOT/'docs/validation/ShipAI_Corpse_Integration_20261031.json';dest.write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
print(json.dumps({'runtime_passed':True,'asset_global_passed':False,'corpse_frames':result['corpse']['frames'],'motion_frames':sum(c['frames'] for c in motion['cases']),'images':images,'report':str(dest)},ensure_ascii=False,indent=2))
