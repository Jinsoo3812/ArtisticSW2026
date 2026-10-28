"""Install reproducible checks for probe_world_audio.py's run_script action."""
from pathlib import Path
import shutil

root=Path(__file__).resolve().parents[2]
dest=root/'Saved/AudioIntegration/LiveScripts'
dest.mkdir(parents=True,exist_ok=True)
for path in (Path(__file__).parent/'AudioPIE').glob('*.py'):
    shutil.copy2(path,dest/path.name)
print(dest)
