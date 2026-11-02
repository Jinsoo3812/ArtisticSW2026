"""Resume a still-live validation after a tooling error, preserving completed checks."""
import json
from pathlib import Path
scripts=OUT.parent.parent/'LiveScripts'
progress=json.loads((OUT/'cannon_validation_progress.json').read_text())
resume=next(i for i,r in enumerate(progress) if not r['complete'])
assert progress[resume]['stage']=='ground_policy','Only the read-only ground check immediately before socket editing is resumable here'
scope={'unreal':unreal,'OUT':OUT,'live_api':live_api,'result':None}
exec(compile((scripts/'validation_sequence.py').read_text(encoding='utf-8-sig'),str(scripts/'validation_sequence.py'),'exec'),scope)
assert scope['steps'][resume][0]==progress[resume]['stage']
scope['index']=resume-1;scope['progress']=progress[:resume]
done=False

def on_tick(delta):
 global done
 if done:return
 scope['on_tick'](delta)
 if scope.get('done'):
  (OUT/'cannon_run_complete.json').write_text(json.dumps({'native_test':True,'PIE_sequence':True,'continued_after_tooling_error':'Use public StaticMesh.find_socket instead of reading protected Sockets property'}))
  done=True
result={'requested':'resume from ground policy before socket edit; preserve completed native and lifecycle checks'}
