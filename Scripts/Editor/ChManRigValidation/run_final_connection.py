from pathlib import Path
source=Path(__file__).with_name('run_pie_comparison.py')
SAVED_CONNECTION_MODE=True
SAVED_CASES=[('saved_final_60',60,None,None)]
RESULT_FILENAME='final_saved_connection.json'
exec(compile(source.read_text(encoding='utf8'),str(source),'exec'))
