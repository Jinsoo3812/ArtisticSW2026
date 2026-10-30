"""Build a bounded evidence report from one completed two-player audio probe."""
import argparse
from collections import Counter
import json
from pathlib import Path
import re

p=argparse.ArgumentParser()
p.add_argument('--session',type=Path,required=True)
p.add_argument('--log',type=Path,required=True)
args=p.parse_args()
lines=args.log.read_text(encoding='utf-8',errors='replace').splitlines()
phase=None;events=[];shots=[];completed=[]
for line in lines:
    for marker,kind in [('SW_NETWORK_CANNON_PHASE ','cannon'),('SW_NETWORK_ENTRY_PHASE ','entry'),('SW_NETWORK_MOTION_PHASE ','motion')]:
        if marker in line:
            phase=(kind,line.split(marker,1)[1].strip())
            if kind=='cannon':events=[e for e in events if e['phase']!=phase]
    match=re.search(r'Submit World=(\S+) NetMode=(\d+) Event=(\d+) Actor=(\S+).* Sound=(\S+)',line)
    if match and phase:
        world,mode,event,actor,sound=match.groups()
        events.append({'phase':phase,'mode':int(mode),'event':int(event),'actor':actor,'sound':sound})
    if 'LogTemp: Display: SW_NATIVE_AUDIO_SHOT World=' in line:shots.append(line)
    if 'Test Completed. Result=' in line:completed.append(line)
    if any(m in line for m in ['SW_NETWORK_CANNONS_DONE','SW_NETWORK_ENTRIES_DONE','SW_NETWORK_MOTION_DONE']):phase=None

checks={}
for label in ['player','enemy']:
    # If a phase was repeated, use its latest projectile and the latest shot outcome.
    hits=[e for e in events if e['phase']==('cannon',label) and e['event']==1]
    projectile=hits[-1]['actor'] if hits else None
    checks[label+'_water_impact_modes']=sorted({e['mode'] for e in hits if e['actor']==projectile})
    fires=[e for e in events if e['phase']==('cannon',label) and e['event']==0]
    checks[label+'_fire_modes']=sorted({e['mode'] for e in fires})
    checks[label+'_fire_counts']=dict(Counter(e['mode'] for e in fires))
    assert checks[label+'_water_impact_modes']==[2,3],checks
    assert checks[label+'_fire_modes']==[2,3],checks
    assert checks[label+'_fire_counts']=={2:1,3:1},checks
assert all('Accepted=1 CooldownAttempt=0' in s for s in shots[-2:]),shots[-2:]

entries={}
for e in events:
    if e['phase'][0]=='entry' and e['event']==3:entries.setdefault(e['actor'],set()).add(e['mode'])
assert len(entries)==2 and all(m=={2,3} for m in entries.values()),entries
motion=json.loads((args.session/'MotionVerified/network_motion_samples.json').read_text())
motion_checks={}
for label in ['player','enemy','both','idle','stopped','far','returned']:
    rows=[r for r in motion if r['phase']==label]
    assert rows,label
    last=rows[-1]
    states=[w['audio'] for w in last['worlds']]
    counts=[len(re.findall(r'Playing=1 Releasing=0',s)) for s in states]
    expected=2 if label in ['both','returned'] else (1 if label in ['player','enemy'] else 0)
    assert counts==[expected,expected],(label,states)
    motion_checks[label]={'active_loops':counts,'final_state':states}
wave_counts=Counter((e['actor'],e['mode']) for e in events if e['phase'][0]=='motion' and e['event']==14)
assert len(wave_counts)>=4,wave_counts
signals={}
for folder in ['MotionVerified','CannonsVerified','EntriesVerified']:
    signals[folder]=json.loads((args.session/folder/'network_signal_summary.json').read_text())
    assert all(r['clipped_samples']==0 for r in signals[folder]),folder
    active=[r for r in signals[folder] if not any(x in r['file'] for x in ['idle','stopped','far'])]
    assert active and all(r['active_samples']>0 for r in active),(folder,active)
assert len(completed)==14 and all('Result={Success}' in l for l in completed),completed
report={'map':'/Game/Level/Lvl_CY','session':str(args.session.resolve()),'log':str(args.log.resolve()),
        'two_player_listen_server_client':True,'automation_passed':14,'cannon_checks':checks,
        'native_shot_results':shots[-2:],'character_entry_modes':{a:sorted(m) for a,m in entries.items()},
        'motion':motion_checks,'wave_impact_counts':{f'{actor}/NetMode{mode}':n for (actor,mode),n in wave_counts.items()},
        'signals':signals,'remaining':['subjective timbre acceptance','late join / artificial lag and packet loss','all ship/enemy variants','cook / packaged run','actual relative current and cabin occlusion'],
        'interpretation':'Native submissions plus separate device recordings establish actual output, not subjective listening quality. Silent submix intervals may be omitted from WAV.'}
out=args.session.parents[1]/'ShipWaterValidation_20261028.json'
out.write_text(json.dumps(report,indent=2),encoding='utf-8')
print(out)
