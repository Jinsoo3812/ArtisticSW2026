"""Summarize the actual local Enhanced Input test in two-player PIE."""
import json
from pathlib import Path
import sys

root=Path(sys.argv[1])
rows=json.loads((root/'manual_zoom_samples.json').read_text())
names=['host_zoom_in','client_zoom_out','minimum','maximum','ui_blocks','selection_blocks','restore','water_zoom_in','water_zoom_out','final']
expected=[[350,400],[350,450],[150,450],[800,450],[800,450],[800,450],[400,400],[400,350],[450,350],[450,350]]
summary=[]
for name,distances in zip(names,expected):
    stable=[r for r in rows if r['phase']==name and r['elapsed']>1.8]
    assert stable,name
    for peer,distance in enumerate(distances):
        samples=[r['worlds'][peer] for r in stable]
        assert all(abs(s['preferred']-distance)<.01 for s in samples),(name,peer,samples)
        assert all(not s['collision'] for s in samples),(name,peer,'collision on')
        assert all(s['swimming']==name.startswith(('water_','final')) for s in samples),(name,peer,'unexpected movement state')
        summary.append({'phase':name,'peer':peer,'samples':len(samples),'preferred_cm':distance,
            'last_target_cm':samples[-1]['target'],'collision':False,'swimming':samples[-1]['swimming']})
result={'date':'2026-10-30','map':'/Game/Level/Lvl_CY','method':'Two-player listen server/client PIE; native zoom InputAction values injected through EnhancedInputLocalPlayerSubsystem; teleport setup for cabin/water. Physical wheel and every UI widget were not driven by this script.',
    'samples':len(rows),'stable_summary':summary,
    'mapping_audit':json.loads((root/'manual_zoom_mapping_audit.json').read_text()),
    'raw_directory':str(root),'assertions_passed':True}
out=Path('docs/validation/Player_Manual_Zoom_20261030.json')
out.write_text(json.dumps(result,indent=2))
print(json.dumps({'output':str(out),'samples':len(rows),'passed':True}))
