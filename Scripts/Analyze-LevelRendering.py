"""Summarize one Insights export directory, without adding nested GPU scopes twice.

Inputs: gpu_events.csv and game_stats.csv from Export-LevelInsights.ps1.
The first/last two seconds are excluded. Output: summary.json.
"""
import csv, json, bisect, statistics, sys
from collections import Counter, defaultdict
from pathlib import Path

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).parent
events = []
with (root/'gpu_events.csv').open(encoding='utf-8-sig', newline='') as f:
    for row in csv.DictReader(f):
        if row['ThreadId'] == '65536':
            events.append((float(row['StartTime']), float(row['EndTime']), int(row['Depth']), row['TimerName']))
frames = sorted(e for e in events if e[3] == 'Frame' and e[2] == 0)
if not frames:
    raise SystemExit('No graphics GPU frames found in gpu_events.csv')
starts = [e[0] for e in frames]
lo, hi = starts[0]+2, frames[-1][1]-2
valid = [i for i, e in enumerate(frames) if e[0] >= lo and e[1] <= hi]
valid_set = set(valid)
if len(valid) < 30:
    raise SystemExit('Capture is too short after removing warmup/tail frames')
by_name = defaultdict(lambda: defaultdict(list))
for a,b,d,n in events:
    i = bisect.bisect_right(starts, a)-1
    if i in valid_set:
        by_name[n][i].append((a,b,d))
def union(spans):
    end, total = -1, 0
    for a,b,*_ in sorted(spans):
        if b > end:
            total += b-max(a,end)
            end = b
    return total*1000
def desc(vals):
    return {'mean_ms':statistics.mean(vals), 'p95_ms':sorted(vals)[int(len(vals)*.95)], 'max_ms':max(vals)}
summary = {'central_frames':len(valid), 'central_seconds':hi-lo,
    'frame_interval':desc([(frames[i+1][0]-frames[i][0])*1000 for i in valid if i+1<len(frames)]),
    'graphics_frame_span':desc([(frames[i][1]-frames[i][0])*1000 for i in valid]), 'passes':{},
    'scene_depths':dict(Counter(d for a,b,d,n in events if n=='SceneRender')),
    'scene_events_per_frame':dict(Counter(len(by_name['SceneRender'][i]) for i in valid))}
for n in ['PrePass %s %s','BasePass','NaniteBasePass','Nanite::BasePass','RenderVelocities(%s)','ShadowDepths',
        'SceneRender','SingleLayerWater','LumenReflections','PostProcessing','TemporalSuperResolution',
        'Nanite::VisBuffer','Nanite::DrawGeometry']:
    summary['passes'][n] = desc([union(by_name[n][i]) for i in valid])
# Actual independent SceneRender spans (nested scopes count once).
summary['independent_scenes_per_frame'] = dict(Counter(sum(1 for j,e in enumerate(sorted(by_name['SceneRender'][i]))
    if not any(p[0]<=e[0] and p[1]>=e[1] for p in sorted(by_name['SceneRender'][i])[:j])) for i in valid))
sample = valid[len(valid)//2]
summary['sample_scenes'] = by_name['SceneRender'][sample]
groups = defaultdict(lambda: defaultdict(list))
stack = []
for a,b,d,n in events:
    while stack and stack[-1][2] >= d:
        stack.pop()
    ancestors = [e[3] for e in stack]
    i = bisect.bisect_right(starts,a)-1
    if i in valid_set:
        if n == 'ParallelDraw' and 'BasePass' in ancestors:
            groups['BasePass_ParallelDraw'][i].append((a,b,d))
        if n == 'ParallelDraw' and 'RenderVelocities(%s)' in ancestors:
            groups['Velocity_ParallelDraw'][i].append((a,b,d))
        if n == 'Batched' and 'ShadowDepths' in ancestors:
            groups['Shadow_Batched'][i].append((a,b,d))
        if n == 'Nanite::DrawGeometry' and 'ShadowDepths' in ancestors:
            groups['Shadow_NaniteDrawGeometry'][i].append((a,b,d))
    stack.append((a,b,d,n))
summary['draw_groups'] = {n:desc([union(spans[i]) for i in valid]) for n,spans in groups.items()}
summary['sample_heavy_pass_children'] = []
heavy = by_name['BasePass'][sample]+by_name['RenderVelocities(%s)'][sample]+by_name['ShadowDepths'][sample]
for a,b,d,n in events:
    if any(x<=a and b<=y for x,y,z in heavy) and b-a>0.00002:
        summary['sample_heavy_pass_children'].append({'name':n,'depth':d,'ms':(b-a)*1000})
summary['game_thread'] = {}
with (root/'game_stats.csv').open(encoding='utf-8-sig', newline='') as f:
    stats={r['Name']:r for r in csv.DictReader(f)}
ticks=int(stats['FEngineLoop::Tick']['Count'])
summary['app_frames']=ticks
for name in ['FEngineLoop::Tick','Game thread idle time','World Tick Time','Total Slate Tick Time',
    'ExecuteUbergraph_BP_StylizedWeather','SW_Ship_Tick','SW_Wake_Tick','ALandscapeProxy::UpdateGrass']:
    if name in stats:
        r=stats[name]
        summary['game_thread'][name]={'incl_ms_per_app_frame':float(r['Incl'])/ticks*1000,'count':int(r['Count'])}
(root/'summary.json').write_text(json.dumps(summary,indent=2),encoding='utf-8')
for name in ['PrePass %s %s', 'BasePass', 'RenderVelocities(%s)', 'ShadowDepths', 'SceneRender']:
    print(f"{name}: {summary['passes'][name]['mean_ms']:.3f} ms")
print(f"Frames: {len(valid)}; summary: {root / 'summary.json'}")
