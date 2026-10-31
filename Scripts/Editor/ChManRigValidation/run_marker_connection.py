from pathlib import Path
source=Path(__file__).with_name('run_pie_comparison.py')
SAVED_CONNECTION_MODE=True
SAVED_CASES=[('final_markers_30',30,None,None),('final_markers_60',60,None,None),('final_markers_120',120,None,None)]
RESULT_FILENAME='marker_saved_connection.json'
exec(compile(source.read_text(encoding='utf8'),str(source),'exec'))
