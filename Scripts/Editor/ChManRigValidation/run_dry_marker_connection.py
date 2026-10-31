from pathlib import Path
source=Path(__file__).with_name('run_pie_comparison.py')
SAVED_CONNECTION_MODE=True
SAVED_CASES=[('dry_final_30',30,None,None),('dry_final_60',60,None,None),('dry_final_120',120,None,None)]
RESULT_FILENAME='dry_marker_saved_connection.json'
FLOOR_Z=1000
exec(compile(source.read_text(encoding='utf8'),str(source),'exec'))
