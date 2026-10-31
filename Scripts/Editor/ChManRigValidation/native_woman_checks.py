from pathlib import Path
scripts=Path(__file__).parent
for script in ['audit_woman_source_scales.py','verify_woman_bake.py']:
    path=scripts/script;exec(compile(path.read_text(encoding='utf8'),str(path),'exec'))
