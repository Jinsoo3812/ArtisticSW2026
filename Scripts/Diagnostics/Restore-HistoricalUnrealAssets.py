"""Restore only absent, proven historical packages with normal Git/LFS filters."""
import hashlib,json,subprocess
from collections import defaultdict
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'Saved/AssetRepair'
rows=json.loads((OUT/'missing_history.json').read_text(encoding='utf8'))['rows']
groups=defaultdict(list)
for row in rows:
    if row['found']:
        assert not (ROOT/row['path']).exists(),row['path']
        groups[row['source_commit']].append(row)
restored=[]
for commit,items in groups.items():
    subprocess.run(['git','restore','--source='+commit,'--worktree','--',*[r['path'] for r in items]],cwd=ROOT,check=True)
    for row in items:
        path=ROOT/row['path'];assert path.stat().st_size==row['bytes'],row
        if 'lfs_sha256' in row:assert hashlib.sha256(path.read_bytes()).hexdigest()==row['lfs_sha256'],row
        with path.open('rb') as f:assert f.read(4)==b'\xc1\x83\x2a\x9e',row
        restored.append(row)
(OUT/'historical_restoration.json').write_text(json.dumps({'restored':restored,'passed':True},ensure_ascii=False,indent=2),encoding='utf8')
print('Restored and verified historical packages:',len(restored))
