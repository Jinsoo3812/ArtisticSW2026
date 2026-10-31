from pathlib import Path
source=Path(__file__).with_name('run_pie_comparison.py')
SAVED_CONNECTION_MODE=True
SAVED_CASES=[('sprint_probe',60,None,None)]
RESULT_FILENAME='sprint_probe_run.json'
FLOOR_Z=1000
exec(compile(source.read_text(encoding='utf8'),str(source),'exec'))
