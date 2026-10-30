from pathlib import Path
scripts=OUT.parent.parent/'LiveScripts'
exec(compile((scripts/'move_grips_to_barrel.py').read_text(encoding='utf-8-sig'),str(scripts/'move_grips_to_barrel.py'),'exec'),globals())
code=(scripts/'run_validation.py').read_text(encoding='utf-8-sig').replace("run('validation_sequence.py')","run('barrel_validation_sequence.py')")
exec(compile(code,str(scripts/'run_validation.py'),'exec'),globals())
