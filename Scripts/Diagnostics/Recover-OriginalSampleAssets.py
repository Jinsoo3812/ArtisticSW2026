"""Recover the exact original enum; newer experimental schemas use a separate export path."""
import hashlib,json,re,shutil
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'Saved/AssetRepair'
SOURCE=Path('C:/Users/I/Documents/Unreal Projects/GameAnimationSample/Content')
paths=['Blueprints/AnimNotifies/E_FoleyEventSide.uasset']
rows=[]
for name in paths:
    source=SOURCE/name;dest=ROOT/'Content'/name
    assert source.is_file(),name
    data=source.read_bytes();assert data[:4]==b'\xc1\x83\x2a\x9e'
    branches=re.findall(rb'\+\+UE5\+Release-5\.[0-9]+',data)
    assert branches and all(int(b.rsplit(b'.',1)[1])<=7 for b in branches),branches
    if not dest.exists():dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,dest)
    assert hashlib.sha256(dest.read_bytes()).digest()==hashlib.sha256(data).digest()
    rows.append({'source':str(source),'target':str(dest.relative_to(ROOT)),'sha256':hashlib.sha256(data).hexdigest(),'bytes':len(data),'saved_branches':[b.decode() for b in branches]})
(OUT/'original_sample_restoration.json').write_text(json.dumps({'passed':True,'rows':rows},ensure_ascii=False,indent=2),encoding='utf8')
print('Recovered exact original sample assets:',len(rows))
