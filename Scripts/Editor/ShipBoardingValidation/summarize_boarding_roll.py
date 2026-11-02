"""Summarize owner C presses and authoritative roll state per real sea-boarding case."""
import json,sys
from pathlib import Path

def summarize(folder):
 rows=json.loads((folder/'boarding_roll_samples.json').read_text());complete=json.loads((folder/'boarding_roll_complete.json').read_text());result=[]
 names=[]
 for c in complete['cases']:
  if c['player'] not in names:names.append(c['player'])
 for c in complete['cases']:
  owner=names.index(c['player']);samples=[r for r in rows if r['round']==c['round'] and r['player']==c['player'] and r['elapsed']>=9]
  local=[r for r in samples if r['peer']==owner];server=[r for r in samples if r['peer']==0]
  result.append({**c,'local_grounded_seen':any(r['grounded'] for r in local),'server_grounded_seen':any(r['grounded'] for r in server),'stale_local_swim_tag':any(r['grounded'] and not r['swimming'] and r['swim_tag']>0 for r in local),'stale_server_swim_tag':any(r['grounded'] and not r['swimming'] and r['swim_tag']>0 for r in server),'local_roll_seen':any(r['rolling_tag']>0 or r.get('roll_active',False) for r in local),'server_roll_seen':any(r['rolling_tag']>0 or r.get('roll_active',False) for r in server),'local_can_roll_seen':any(r['roll_can_activate'] for r in local)})
 return result
if __name__=='__main__':
 before,after=map(Path,sys.argv[1:3]);result={'before':summarize(before),'after':summarize(after)}
 assert any(c['stale_server_swim_tag'] for c in result['before'])
 assert len(result['after'])==8
 for c in result['after']:
  assert c['water']['swimming'] and c['water']['swim_tag']>0,c
  assert not c['stale_local_swim_tag'] and not c['stale_server_swim_tag'],c
  assert c['local_roll_seen'] and c['server_roll_seen'],c
 path=Path(sys.argv[3]);path.parent.mkdir(parents=True,exist_ok=True);path.write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
