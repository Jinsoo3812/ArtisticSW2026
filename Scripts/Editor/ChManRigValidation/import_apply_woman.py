from pathlib import Path
scripts=Path(__file__).parent
exec(compile((scripts/'import_woman.py').read_text(encoding='utf8'),str(scripts/'import_woman.py'),'exec'))
assert report.get('passed'),report
exec(compile((scripts/'apply_woman.py').read_text(encoding='utf8'),str(scripts/'apply_woman.py'),'exec'))
