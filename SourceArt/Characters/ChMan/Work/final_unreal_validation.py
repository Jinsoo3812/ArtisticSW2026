from pathlib import Path
folder=Path(__file__).parent
for name in ['import_verify_chman.py','evaluate_chman_poses.py']:
    path=folder/name
    exec(compile(path.read_text(encoding='utf8'),str(path),'exec'),{'__file__':str(path),'__name__':'__main__'})
