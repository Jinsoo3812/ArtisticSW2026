"""Offline acceptance for contact-driven lateral spine motion; no Unreal imports."""
import json, math, sys
from pathlib import Path

def mul(a,b):
 x,y,z,w=a;X,Y,Z,W=b
 return [w*X+x*W+y*Z-z*Y,w*Y-x*Z+y*W+z*X,w*Z+x*Y-y*X+z*W,w*W-x*X-y*Y-z*Z]
def relative(a,b):return mul([-a[0],-a[1],-a[2],a[3]],b)
def angle(a,b):return math.degrees(2*math.acos(min(1,abs(sum(x*y for x,y in zip(a,b))))))
def summarize(folder):
 rows=json.loads((folder/'cannon_network_samples.json').read_text());groups={}
 for sample in rows:
  for peer,world in enumerate(sample['worlds']):
   for p in world['operators']:groups.setdefault(str(peer)+':'+p['name'],[]).append((sample,p))
 metrics={}
 for key,samples in groups.items():
  by_phase={};sides=[];curves=[];limbs={};segments={}
  for sample,p in samples:
   b=p['aim_bone_positions'];hip=b['pelvis'];shoulder=[sum(b[k][i] for k in ['upperarm_l','upperarm_r'])/2 for i in range(3)]
   grip=[sum(p['contact_actor_positions'][k][i] for k in ['hand_l','hand_r'])/2 for i in range(3)]
   if abs(grip[1]-hip[1])>15:
    direction=1 if grip[1]>hip[1] else -1
    sides.append((shoulder[1]-hip[1])*direction)
   chain=['pelvis','spine_01','spine_02','spine_03','spine_04','spine_05']
   for a,z in zip(chain,chain[1:]):segments.setdefault(z,[]).append(math.dist(b[a],b[z]))
   q=p['aim_bone_quats'];curves.append(relative(q['spine_01'],q['spine_05']))
   for k,v in p['limb_lengths'].items():limbs.setdefault(k,[]).append(v)
   phase=by_phase.setdefault(str(sample['phase']),{'yaw':sample['target_yaw'],'pitch':sample['target_pitch'],'max_hand_error_cm':0})
   phase['max_hand_error_cm']=max(phase['max_hand_error_cm'],*p['hand_errors'].values())
  metrics[key]={'minimum_shoulder_shift_toward_grips_cm':min(sides),'spine_curvature_change_deg':max(angle(curves[0],q) for q in curves),'max_foot_error_cm':max(max(p['foot_errors'].values()) for _,p in samples),'max_limb_length_ratio':max(max(v)/min(v) for v in limbs.values()),'max_spine_segment_length_ratio':max(max(v)/min(v) for v in segments.values()),'max_hand_error_cm':max(x['max_hand_error_cm'] for x in by_phase.values()),'phases':by_phase}
  m=metrics[key]
  assert m['minimum_shoulder_shift_toward_grips_cm']>1,m
  assert m['spine_curvature_change_deg']>2,m
  assert m['max_foot_error_cm']<1,m
  assert m['max_limb_length_ratio']<1.01,m
  assert m['max_spine_segment_length_ratio']<1.01,m
 exits=json.loads((folder/'cannon_exit_checks.json').read_text())
 assert all(p['is_character'] and p['attached'] is None and 'WALKING' in p['movement'] and not any(p['cannon_hides']) for p in exits)
 steps={};last={}
 for row in json.loads((folder/'cannon_exit_continuity.json').read_text()):
  key=str(row['peer'])+':'+row['player']
  if key in last:
   for bone,pos in row['points'].items():steps[bone]=max(steps.get(bone,0),math.dist(pos,last[key]['points'][bone]))
  last[key]=row
 assert max(steps.values())<15,steps
 assert max(steps['foot_l'],steps['foot_r'])<3,steps
 return {'source':str(folder),'contact_side_and_articulated_spine_passed':True,'metrics':metrics,'exit_max_bone_step_cm':steps,'exit_state_restored':True,'all_hands_within_1cm':all(m['max_hand_error_cm']<1 for m in metrics.values())}
if __name__=='__main__':
 result=summarize(Path(sys.argv[1]));output=Path(sys.argv[2]);output.parent.mkdir(parents=True,exist_ok=True);output.write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
