from pathlib import Path
RAGDOLL_CASES=[(label,fps,mode) for label in ['Man','Woman'] for fps,mode in [(30,'idle'),(60,'idle'),(120,'idle'),(60,'run'),(60,'sprint'),(60,'jump'),(60,'platform')]]
RAGDOLL_FILENAME='pie_full.json'
source=Path(__file__).with_name('run_ragdoll_pie.py');exec(compile(source.read_text(encoding='utf8'),str(source),'exec'))
