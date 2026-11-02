from pathlib import Path
scripts=OUT.parent.parent/'LiveScripts'
exec(compile((scripts/'author_targets.py').read_text(encoding='utf-8-sig'),str(scripts/'author_targets.py'),'exec'),globals())
exec(compile((scripts/'run_validation.py').read_text(encoding='utf-8-sig'),str(scripts/'run_validation.py'),'exec'),globals())
