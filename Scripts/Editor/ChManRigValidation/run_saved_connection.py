from pathlib import Path
source=Path(__file__).with_name('run_pie_comparison.py')
SAVED_CONNECTION_MODE=True
exec(compile(source.read_text(encoding='utf8'),str(source),'exec'))
