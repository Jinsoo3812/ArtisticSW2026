import json
from pathlib import Path
OUT=Path(__file__).resolve().parents[3]/'Saved/ChManRigValidation'
r=json.loads((OUT/'pie_comparison.json').read_text(encoding='utf8'))
summary={'errors':r['errors'],'cases':[]}
for c in r['cases']:
    phases=c['phase_stats']
    summary['cases'].append({k:c[k] for k in ['name','frames','requested_fps','sampled_seconds','nonfinite','max_radius_cm','max_scale_error','max_limb_length_error_cm','max_local_rotation_step_deg','max_rotation_speed_deg_s','outliers']})
    row=summary['cases'][-1]
    row['effective_sample_fps']=(c['frames']-1)/c['sampled_seconds'] if c.get('sampled_seconds',0) else 0
    row['database_count']=len(c['databases']);row['animation_count']=len(c['animations'])
    row['databases']=list(c['databases'])
    row['phases']={n:{k:p[k] for k in ['frames','active_mm_frames','invalid_active_mm_frames','max_speed','air_frames','max_limb_length_error_cm']} for n,p in phases.items()}
(OUT/'pie_summary.json').write_text(json.dumps(summary,indent=2),encoding='utf8')
print(json.dumps(summary,indent=2))
