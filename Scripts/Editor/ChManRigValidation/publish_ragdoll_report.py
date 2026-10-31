"""Publish bounded, reviewable final physics/lifecycle evidence (ordinary Python)."""
import json,math
from pathlib import Path
root=Path(__file__).resolve().parents[3]
saved=root/'Saved/PlayerRagdollValidation'
read=lambda name:json.loads((saved/name).read_text(encoding='utf-8-sig'))
pie=read('pie_full.json');network=read('network.json');automation=read('Automation/index.json')
assert not pie['errors'] and len(pie['cases'])==14
assert network.get('passed') and not network['errors'] and len(network['samples'])>=10
assert automation['failed']==0 and automation['notRun']==0 and len(automation['tests'])==23
cases=[]
for c in pie['cases']:
    final=c['snapshots'][-1]
    angular=max(j['angular_excess_deg'] for j in final['joints'])
    anchor=max(j['anchor_error_cm'] for j in final['joints'])
    assert c['frames']>200 and c['effective_fps']>=c['requested_fps']*.85
    assert c['nonfinite']==0 and c['restored_nonfinite']==0 and c['restored_not_simulating'] and c['restored_anim_unpaused']
    assert c['restored_speed']>200 and c['duplicate_velocity_delta']<.01
    assert c['max_render_body_error_cm']<.01
    # Transient impact error is measured, not advertised as a zero-error hard stop.
    assert c['max_limit_excess_deg']<15 and c['max_anchor_error_cm']<5,(c['character'],c['mode'],'impact',c['worst_joint'])
    assert angular<2 and anchor<.3,(c['character'],c['mode'],'settled',angular,anchor)
    assert c['late_max_speed_cm_s']<(40 if c['mode']=='platform' else 5)
    assert all(b['position_iterations']==64 and b['velocity_iterations']==16 for s in c['snapshots'] for b in s['bodies'])
    if c['mode']=='platform':assert math.dist(c['initial_base_momentum'],[250,0,0])<.1
    row={k:c[k] for k in ['character','requested_fps','mode','frames','effective_fps','nonfinite','max_speed_cm_s','max_angular_speed_rad_s','max_limit_excess_deg','max_anchor_error_cm','max_render_body_error_cm','minimum_com_above_floor_cm','late_max_speed_cm_s','duplicate_velocity_delta','restored_speed','worst_joint']}
    row.update(final_limit_excess_deg=angular,final_anchor_error_cm=anchor)
    cases.append(row)
summary={'date':'2026-10-31','engine':'UE 5.7.4','passed':True,'bodies_per_character':17,'joints_per_character':16,
 'world_physics':'Existing async 60Hz; no global physics configuration changes',
 'angular_metric':'Actual joint frames, swing/twist decomposition, Chaos elliptical GetConeAngleLimit radius',
 'acceptance':{'impact_limit_excess_deg_below':15,'impact_anchor_error_cm_below':5,'final_limit_excess_deg_below':2,'final_anchor_error_cm_below':.3,'render_body_error_cm_below':.01,'late_speed_stationary_cm_s_below':5,'late_speed_relative_to_platform_cm_s_below':40},
 'cases':cases,'network':{'passed':True,'worlds':network['worlds'],'sample_count':len(network['samples']),'corpse_replicas_per_sample':4,'new_lives':network['new_lives'],'policy':'Replicated death/impact payload, locally simulated cosmetic corpses; body poses are not synchronized'},
 'automation':{'passed':automation['succeeded']+automation['succeededWithWarnings'],'with_warnings':automation['succeededWithWarnings'],'failed':automation['failed'],'tests':[{'path':t['fullTestPath'],'state':t['state']} for t in automation['tests']]},
 'limits':['Flat translating ShipDeck proxy, not rotating/wave-driven/sinking ship or underwater buoyancy','No dedicated server, latency/loss, full room travel/reconnect or cook/package test','No cloth/hair simulation; visual surfaces may intersect floor/body','No mass-corpse performance benchmark; existing 5-second production corpse lifetime retained'],
 'source_art':'SourceArt/Characters/Physics','raw_results':'Saved/PlayerRagdollValidation'}
render=saved/'render_review.json'
if render.exists():summary['render_review']=json.loads(render.read_text(encoding='utf8'))
dest=root/'docs/validation/Player_Ragdoll_20261031.json';dest.parent.mkdir(exist_ok=True);dest.write_text(json.dumps(summary,ensure_ascii=False,indent=2),encoding='utf8')
print(json.dumps({'passed':True,'cases':len(cases),'frames':sum(c['frames'] for c in cases),'max_impact_limit_excess_deg':max(c['max_limit_excess_deg'] for c in cases),'max_impact_anchor_cm':max(c['max_anchor_error_cm'] for c in cases),'max_final_limit_excess_deg':max(c['final_limit_excess_deg'] for c in cases),'max_final_anchor_cm':max(c['final_anchor_error_cm'] for c in cases),'automation':summary['automation']['passed'],'report':str(dest)},indent=2))
