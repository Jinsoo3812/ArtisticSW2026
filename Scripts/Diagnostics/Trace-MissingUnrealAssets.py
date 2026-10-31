"""Trace missing package files through existing Git history; no working tree writes."""
import concurrent.futures,json,subprocess,re
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'Saved/AssetRepair';OUT.mkdir(parents=True,exist_ok=True)
def git(*args):return subprocess.check_output(['git',*args],cwd=ROOT,stderr=subprocess.DEVNULL)
refs=json.loads((ROOT/'Saved/ShipAICorpseIntegration/engine_asset_audit.json').read_text())['missing_hard_references']
targets=sorted({r['to'] for r in refs}|{'/Game/Blueprints/AnimNotifies/E_FoleyEventSide'})
def inspect(package):
    paths=['Content/'+package.removeprefix('/Game/')+ext for ext in ['.uasset','.umap']]
    commits=git('log','--all','-12','--format=%H','--',*paths).decode().splitlines()
    row={'package':package,'referencers':[r['from'] for r in refs if r['to']==package],'history_count':len(commits),'found':False}
    if commits:row['latest_change']=git('show','-s','--format=%H %aI %s',commits[0]).decode().strip()
    for commit in commits:
        tree=git('ls-tree','-z',commit,'--',*paths).split(b'\0')
        for entry in tree:
            if not entry:continue
            meta,path=entry.split(b'\t',1);oid=meta.split()[2].decode();blob=git('cat-file','blob',oid)
            if blob.startswith(b'version https://git-lfs.github.com/spec/v1\n'):
                info=blob.decode();size=int(re.search(r'size (\d+)',info)[1]);sha=re.search(r'oid sha256:([a-f0-9]+)',info)[1]
                row.update(lfs_sha256=sha,bytes=size)
            else:row['bytes']=len(blob)
            row.update(found=True,source_commit=commit,path=path.decode(),blob_oid=oid)
            return row
    return row
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:rows=list(pool.map(inspect,targets))
result={'targets':len(rows),'found':sum(r['found'] for r in rows),'rows':rows}
(OUT/'missing_history.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
print(json.dumps({'targets':len(rows),'found':result['found'],'missing':[r['package'] for r in rows if not r['found']],'total_recoverable_bytes':sum(r.get('bytes',0) for r in rows)},ensure_ascii=False,indent=2))
