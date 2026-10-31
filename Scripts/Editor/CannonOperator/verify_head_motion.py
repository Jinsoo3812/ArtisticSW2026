from pathlib import Path
source=(OUT.parent.parent/'LiveScripts/run_validation.py').read_text(encoding='utf-8-sig')
source=source.replace("run('setup_pie.py');scope=run('validation_sequence.py')","scope=run('head_motion_checks.py')")
exec(compile(source,'head-motion-validation','exec'),globals())
