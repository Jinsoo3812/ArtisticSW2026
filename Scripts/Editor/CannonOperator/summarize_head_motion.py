"""Compare head/chest relative rotation every frame; sparse position samples miss neck snaps."""
import json,math,sys
from pathlib import Path

def mul(a,b):
 x,y,z,w=a;X,Y,Z,W=b
 return [w*X+x*W+y*Z-z*Y,w*Y-x*Z+y*W+z*X,w*Z+x*Y-y*X+z*W,w*W-x*X-y*Y-z*Z]
def relative(a,b):return mul([-a[0],-a[1],-a[2],a[3]],b)
def angle(a,b):return math.degrees(2*math.acos(min(1,abs(sum(x*y for x,y in zip(a,b))))))
def summarize(path):
 rows=json.loads(path.read_text());last={};metrics={}
 for row in rows:
  key=str(row['peer'])+':'+row['player'];prev=last.get(key);last[key]=row
  if not prev or not row['steady_sweep'] or not prev['steady_sweep'] or row['phase']!=prev['phase']:continue
  dt=row['time']-prev['time']
  if dt<=0 or dt>.1:continue
  head=angle(row['quats']['head'],prev['quats']['head'])
  neck=angle(relative(row['quats']['spine_05'],row['quats']['head']),relative(prev['quats']['spine_05'],prev['quats']['head']))
  m=metrics.setdefault(key,{'max_head_step_deg':0,'max_neck_step_deg':0,'frames':0,'max_hand_error_cm':0,'max_foot_error_cm':0})
  if head>m['max_head_step_deg']:m['max_head_step_deg']=head;m['worst_frame']={k:row[k] for k in ['time','yaw','pitch']}
  m['max_neck_step_deg']=max(m['max_neck_step_deg'],neck);m['frames']+=1
  m['max_hand_error_cm']=max(m['max_hand_error_cm'],row['hand_error']);m['max_foot_error_cm']=max(m['max_foot_error_cm'],row['foot_error'])
 return metrics
if __name__=='__main__':
 before,after=map(Path,sys.argv[1:3]);result={'before':summarize(before),'after':summarize(after)}
 assert len(result['after'])==4
 for m in result['after'].values():
  assert m['frames']>500,m
  assert m['max_head_step_deg']<10,m
  assert m['max_neck_step_deg']<4.1,m
  assert m['max_hand_error_cm']<1 and m['max_foot_error_cm']<1,m
 output=Path(sys.argv[3]);output.parent.mkdir(parents=True,exist_ok=True);output.write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
