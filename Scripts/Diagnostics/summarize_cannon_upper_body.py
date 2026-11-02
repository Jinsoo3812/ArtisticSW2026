"""Validate observed upper-body contribution and head direction against cannon aim."""
import json,math,argparse,collections,re
from pathlib import Path
ap=argparse.ArgumentParser();ap.add_argument('directory',type=Path);ap.add_argument('--output',type=Path,required=True);args=ap.parse_args();root=args.directory
rows=json.loads((root/'cannon_network_samples.json').read_text());groups=collections.defaultdict(list)
def mul(a,b):
 x,y,z,w=a;X,Y,Z,W=b
 return [w*X+x*W+y*Z-z*Y,w*Y-x*Z+y*W+z*X,w*Z+x*Y-y*X+z*W,w*W-x*X-y*Y-z*Z]
def axis(i,deg):
 q=[0.,0.,0.,math.cos(math.radians(deg)/2)];q[i]=math.sin(math.radians(deg)/2);return q
def angle(a,b):
 dot=abs(sum(x*y for x,y in zip(a,b)))/math.sqrt(sum(x*x for x in a)*sum(x*x for x in b))
 return math.degrees(2*math.acos(min(1.,dot)))
assert len({r['phase'] for r in rows})==16
for r in rows:
 for peer,w in enumerate(r['worlds']):
  for p in w['operators']:groups[peer,p['name']].append((r,p))
assert len(groups)==4
summary=[]
for (peer,name),values in groups.items():
 base=values[0][1]['aim_bone_quats'];errors=[]
 for r,p in values:
  expected=mul(mul(axis(2,r['target_yaw']),axis(1,-r['target_pitch'])),base['head'])
  errors.append(angle(expected,p['aim_bone_quats']['head']))
 ranges={b:max(angle(base[b],p['aim_bone_quats'][b]) for r,p in values) for b in ['spine_01','spine_02','spine_03']}
 assert max(errors)<1. and all(a>2 for a in ranges.values()),(name,max(errors),ranges)
 feet=max(a for r,p in values for a in p['foot_errors'].values());assert feet<1.
 summary.append({'peer':peer,'player':name,'max_head_aim_error_deg':max(errors),'spine_rotation_range_deg':ranges,'max_foot_error_cm':feet,'max_hand_contact_error_cm':max(a for r,p in values for a in p['hand_errors'].values())})
previous={};steps=collections.defaultdict(float)
for r in json.loads((root/'cannon_exit_continuity.json').read_text()):
 k=r['peer'],r['player'];old=previous.get(k)
 if old:
  for b,point in r['points'].items():steps[b]=max(steps[b],math.dist(point,old['points'][b]))
 previous[k]=r
assert max(steps.values())<15.,dict(steps)
result={'date':'2026-10-31','upper_body_direction_passed':True,'time_samples':len(rows),'actors':summary,'exit_max_bone_step_cm':dict(steps),'full_hand_reach_passed':all(r['max_hand_contact_error_cm']<1. for r in summary),'raw_directory':str(root),'settings':json.loads((root/'upper_body_settings.json').read_text())}
if 'aim_bone_positions' in rows[0]['worlds'][0]['operators'][0]:
    torso=[]
    for (peer,name),values in groups.items():
        def span(p):
            a=p['aim_bone_positions']['upperarm_l'];b=p['aim_bone_positions']['upperarm_r'];return [y-x for x,y in zip(a,b)]
        def center(p):return [(x+y)/2 for x,y in zip(p['aim_bone_positions']['upperarm_l'],p['aim_bone_positions']['upperarm_r'])]
        base=span(values[0][1]);phi0=math.degrees(math.atan2(base[1],base[0]));start=center(values[0][1]);errors=[];radii=[];motion=[];hips=[]
        for row,player in values:
            v=span(player);phi=math.degrees(math.atan2(v[1],v[0]))-phi0
            errors.append(abs((phi-row['target_yaw']+180)%360-180))
            c=center(player);hip=player['aim_bone_positions']['pelvis'];radii.append(math.dist(c,hip));motion.append(math.dist(c,start));hips.append(math.dist(hip,values[0][1]['aim_bone_positions']['pelvis']))
        assert max(errors)<1. and max(radii)/min(radii)<1.001 and max(hips)<.01
        assert max(motion)>20.,(name,'Shoulders did not travel with the torso')
        torso.append({'peer':peer,'player':name,'max_shoulder_yaw_error_deg':max(errors),'shoulder_motion_cm':max(motion),'waist_shoulder_radius_ratio':max(radii)/min(radii),'max_pelvis_motion_cm':max(hips)})
    result['whole_torso_pivot_passed']=True;result['whole_torso']=torso
args.output.write_text(json.dumps(result,indent=2));print(json.dumps({'upper_body_direction_passed':True,'full_hand_reach_passed':result['full_hand_reach_passed'],'output':str(args.output)}))
