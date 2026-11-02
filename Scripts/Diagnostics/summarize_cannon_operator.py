"""Validate the actual two-player cannon input/pose samples; no Unreal dependency."""
import collections
import json
from pathlib import Path
import argparse
import math
import re

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('pose_directory',type=Path)
parser.add_argument('--lifecycle-directory',type=Path)
parser.add_argument('--output',type=Path,default=Path('docs/validation/Cannon_Operator_20261030.json'))
parser.add_argument('--date',default='2026-10-30')
args=parser.parse_args()
root=args.pose_directory
rows=json.loads((root/'cannon_network_samples.json').read_text())
assert len({r['phase'] for r in rows})==16
groups=collections.defaultdict(list)
for r in rows:
    for peer,w in enumerate(r['worlds']):
        assert len(w['operators'])==2
        for p in w['operators']:
            assert abs(p['actual_yaw']-r['target_yaw'])<1., ('yaw did not reach target',peer,r['phase'],p['name'])
            assert abs(p['actual_pitch']-r['target_pitch'])<1., ('pitch did not reach target',peer,r['phase'],p['name'])
            assert p['hidden_for_this_view']==p['own_operator'], ('local hide mismatch',peer,p['name'])
            assert max(p['hand_errors'].values())<1., ('hand not on control',peer,r['phase'],p['name'],p['hand_errors'])
            assert max(p['foot_errors'].values())<1., ('foot target error',peer,r['phase'],p['name'])
            assert abs(p['root_pitch'])<.5 and abs(p['root_roll'])<.5, 'body inherited pitch/roll'
            groups[(peer,p['name'])].append(p)
summary=[]
for (peer,name),values in groups.items():
    variations={key:max(p['limb_lengths'][key] for p in values)/min(p['limb_lengths'][key] for p in values) for key in values[0]['limb_lengths']}
    assert max(variations.values())<1.01, ('limb stretched',peer,name,variations)
    summary.append({'peer':peer,'player':name,'samples':len(values),
        'max_hand_error_cm':max(v for p in values for v in p['hand_errors'].values()),
        'max_foot_error_cm':max(v for p in values for v in p['foot_errors'].values()),
        'max_root_pitch_deg':max(abs(p['root_pitch']) for p in values),
        'knee_angle_range_deg':[min(v for p in values for v in p['knee_angles'].values()),max(v for p in values for v in p['knee_angles'].values())],
        'max_limb_length_ratio':max(variations.values()),'local_hide_mismatches':0})
result={'date':args.date,'map':'/Game/Level/Lvl_CY','method':'Two-player single-process listen server/client PIE, both native CannonLook InputActions injected on owning local subsystems. Yaw -60/0/60 and pitch -15/0/45 sampled over 8 poses, repeated twice; last 1.5s of each 3s phase sampled.',
    'phase_count':16,'time_samples':len(rows),'operator_observations':len(rows)*4,'assertions_passed':True,'summary':summary,'raw_directory':str(root)}

def continuity(path):
    samples=json.loads(path.read_text());previous={};steps=collections.defaultdict(float)
    deck_step=0.;release_root_step=0.;max_dt=0.;turn_frames=0
    for r in samples:
        if 'TURN_IN_PLACE' in r.get('get_thread_safe_state_controller_presentation_state',''):turn_frames+=1
        key=(r['peer'],r['player']);p=previous.get(key)
        if p:
            max_dt=max(max_dt,r['time']-p['time'])
            for bone,point in r['points'].items():steps[bone]=max(steps[bone],math.dist(point,p['points'][bone]))
            if p['mounted'] and r['mounted']:
                for bone,point in r['deck_feet'].items():deck_step=max(deck_step,math.dist(point,p['deck_feet'][bone]))
            if p['mounted'] and not r['mounted']:
                release_root_step=max(release_root_step,math.dist(r['deck_root'],p['deck_root']))
        previous[key]=r
    return {'observations':len(samples),'max_frame_interval_seconds':max_dt,'max_pose_step_cm':dict(steps),'max_mounted_deck_foot_step_cm':deck_step,'max_dismount_root_step_cm':release_root_step,'unrequested_turn_frames':turn_frames}

if (root/'cannon_aim_continuity.json').exists():
    aim=continuity(root/'cannon_aim_continuity.json')
    assert max(v for k,v in aim['max_pose_step_cm'].items() if k!='head')<1, aim
    assert aim['max_pose_step_cm']['head']<5 and aim['max_mounted_deck_foot_step_cm']<1, aim
    result['aim_continuity']=aim
if (root/'cannon_exit_checks.json').exists():
    result['exit']=json.loads((root/'cannon_exit_checks.json').read_text())
    assert all(r['is_character'] and r['attached'] is None and not any(r['cannon_hides']) for r in result['exit'])
if args.lifecycle_directory:
    lifecycle=args.lifecycle_directory
    checks=json.loads((lifecycle/'cannon_remount_checks.json').read_text())
    assert len(checks['remount'])==2
    assert all(abs(r['yaw_delta']-1)<.01 and r['input_components']==1 for r in checks['remount'])
    assert all(r['is_character'] and r['attached'] is None and r['walking'] and not r['cannon_context_remains'] for r in checks['after_exit_and_destroy'])
    movement=json.loads((lifecycle/'cannon_after_exit_movement.json').read_text())
    assert all(r['moved_cm']>20 and r['server_moved_cm']>20 and r['peak_horizontal_speed']>20 and r['server_peak_horizontal_speed']>20 and r['ship_relative_error_cm']<10 for r in movement), movement
    result['lifecycle']={'raw_directory':str(lifecycle),'checks':checks,'movement':movement,'passed':True}
    if (lifecycle/'cannon_exit_continuity.json').exists():
        release=continuity(lifecycle/'cannon_exit_continuity.json')
        assert max(release['max_pose_step_cm'].values())<15 and release['max_dismount_root_step_cm']<5 and release['unrequested_turn_frames']==0, release
        result['exit_continuity']=release
    if (lifecycle/'cannon_exit_continuity_focus.json').exists():
        first_release=continuity(lifecycle/'cannon_exit_continuity_focus.json')
        assert max(first_release['max_pose_step_cm'].values())<15 and first_release['max_dismount_root_step_cm']<5 and first_release['unrequested_turn_frames']==0, first_release
        result['first_exit_continuity']=first_release
    if (lifecycle/'cannon_possession_state.json').exists():
        ownership=json.loads((lifecycle/'cannon_possession_state.json').read_text())
        assert len(ownership)==3 and all(r['matches'] for r in ownership), ownership
        result['ownership']=ownership
    if (lifecycle/'cannon_after_exit_swim_complete.json').exists():
        swim=json.loads((lifecycle/'cannon_after_exit_swim_samples.json').read_text())
        phases=collections.defaultdict(list)
        for row in swim:
            for peer,view in enumerate(row['worlds']):phases[(row['phase'],peer)].append(view)
        swim_summary=[]
        for peer in [0,1]:
            for suffix in ['surface_forward','underwater_forward','return_to_cabin']:
                values=[v for (phase,p),group in phases.items() if p==peer and phase.endswith(suffix) for v in group]
                assert values,('swim phase missing',peer,suffix)
                if suffix=='surface_forward':assert any(v['swimming'] and not v['underwater'] for v in values)
                if suffix=='underwater_forward':assert all(v['swimming'] and v['underwater'] for v in values)
                if suffix=='return_to_cabin':assert not values[-1]['swimming'] and values[-1]['movement_mode']=='<MovementMode.MOVE_WALKING: 1>'
                swim_summary.append({'peer':peer,'phase':suffix,'samples':len(values),'passed':True})
        result['post_exit_swimming']={'time_samples':len(swim),'summary':swim_summary,'passed':True}
    if (lifecycle/'cannon_unit_passed.json').exists():result['native_automation']=json.loads((lifecycle/'cannon_unit_passed.json').read_text())
    if (lifecycle/'cannon_input_state.json').exists():result['physical_input_mappings']=json.loads((lifecycle/'cannon_input_state.json').read_text())
    if (lifecycle/'cannon_ground_policy.json').exists():result['ground_policy_components_checked']=len(json.loads((lifecycle/'cannon_ground_policy.json').read_text()))
if (root/'cannon_presentation_policy.json').exists():
    policy=json.loads((root/'cannon_presentation_policy.json').read_text())
    assert len(policy['actors'])==4 and all(r['no_control_bar'] and r['original_collider_preserved'] and r['added_visuals_have_no_collision'] and min(r['palm_normal_dot'].values())>.995 for r in policy['actors'])
    result['presentation_isolation']=policy
if (root/'cannon_socket_adjustment.json').exists():
    sockets=json.loads((root/'cannon_socket_adjustment.json').read_text())
    assert sockets['restored'] and len(sockets['actors'])==4
    assert all(abs(r['z_offset_cm']-sockets['offset_cm'])<.01 and r['palm_error_cm']<1. for r in sockets['actors'])
    result['mesh_socket_edit']=sockets
if 'native_automation' in result:
    native_log=Path(result['native_automation']['log']).read_text(encoding='utf-8-sig',errors='replace')
    counts=re.findall(r'Automation Test Queue Empty (\d+) tests performed',native_log)
    assert counts,'No native automation completion marker'
    result['native_automation']['test_count']=int(counts[-1])
out=args.output
out.write_text(json.dumps(result,indent=2))
print(json.dumps({'passed':True,'output':str(out),'observations':result['operator_observations']}))
