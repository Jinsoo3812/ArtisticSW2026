from pathlib import Path
RAGDOLL_CASES=[('Man',60,'jump'),('Woman',60,'run'),('Woman',60,'sprint'),('Woman',60,'jump'),('Man',60,'platform'),('Woman',60,'platform')]
RAGDOLL_FILENAME='pie_stress.json'
source=Path(__file__).with_name('run_ragdoll_pie.py');exec(compile(source.read_text(encoding='utf8'),str(source),'exec'))
