"""Preserve proven orphan packages outside mounted Content; no package data deletion."""
import hashlib,json,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'Saved/AssetRepair'
plan=json.loads((OUT/'orphan_plan.json').read_text(encoding='utf8'))
assert plan['safe_to_archive'] and not plan['outside_referencers']
archive=(ROOT/'SourceArt/Legacy/OrphanWorlds').resolve()
assert archive.is_relative_to(ROOT.resolve())
rows=[]
for package in plan['packages']:
    relative=Path('Content')/(package.removeprefix('/Game/')+'.uasset')
    source=(ROOT/relative).resolve();destination=archive/relative
    assert source.is_relative_to((ROOT/'Content').resolve()) and source.is_file()
    assert not source.is_symlink() and not destination.exists()
    assert not subprocess.check_output(['git','status','--porcelain','--',relative.as_posix()],cwd=ROOT),relative
    data=source.read_bytes();rows.append({'source':relative.as_posix(),'archive':destination.relative_to(ROOT).as_posix(),'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest()})
for row in rows:
    source=ROOT/row['source'];destination=ROOT/row['archive'];destination.parent.mkdir(parents=True,exist_ok=True)
    source.replace(destination)
    assert hashlib.sha256(destination.read_bytes()).hexdigest()==row['sha256'] and not source.exists()
(OUT/'orphan_archive.json').write_text(json.dumps({'kind':'structural containment, not recovery of missing original maps','roots':plan['roots'],'packages':rows,'passed':True},indent=2),encoding='utf8')
print('Preserved outside mounted Content:',len(rows),'packages; bytes:',sum(r['bytes'] for r in rows))
