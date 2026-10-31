"""Read-only audit of tracked Unreal packages against staged Git LFS metadata."""
import hashlib,json,re,subprocess,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'Saved/ShipAICorpseIntegration';OUT.mkdir(parents=True,exist_ok=True)
def git(*args):return subprocess.check_output(['git',*args],cwd=ROOT)
entries=[]
for row in git('ls-files','-s','-z').split(b'\0'):
    if not row:continue
    meta,path=row.split(b'\t',1);mode,oid,stage=meta.decode().split()
    name=path.decode('utf8')
    if name.endswith(('.uasset','.umap')):
        assert stage=='0',f'Unmerged package: {name}'
        entries.append((name,oid))
changed=set(git('diff','--name-only','1c41c6e0','--','Content').decode('utf8').splitlines())
proc=subprocess.Popen(['git','cat-file','--batch'],cwd=ROOT,stdin=subprocess.PIPE,stdout=subprocess.PIPE)
report={'packages':len(entries),'errors':[],'suspicious':[],'hashed_changed_packages':0,'declared_lfs_bytes':0,'smallest':[]}
sizes=[]
for name,oid in entries:
    proc.stdin.write((oid+'\n').encode());proc.stdin.flush()
    header=proc.stdout.readline().decode().split();blob=proc.stdout.read(int(header[2]));assert proc.stdout.read(1)==b'\n'
    target=ROOT/name
    pointer=re.fullmatch(rb'version https://git-lfs.github.com/spec/v1\noid sha256:([a-f0-9]{64})\nsize (\d+)\n',blob)
    if not target.is_file():report['errors'].append({'path':name,'error':'missing working file'});continue
    size=target.stat().st_size;sizes.append((size,name))
    with target.open('rb') as stream:first=stream.read(160)
    if first.startswith(b'version https://git-lfs.github.com/spec/v1'):report['errors'].append({'path':name,'error':'unexpanded LFS pointer','bytes':size});continue
    if size==0:report['errors'].append({'path':name,'error':'zero bytes'});continue
    if first[:4] not in (b'\xc1\x83\x2a\x9e',b'\x9e\x2a\x83\xc1'):report['errors'].append({'path':name,'error':'invalid Unreal package magic','header':first[:12].hex()})
    if size<1024:report['suspicious'].append({'path':name,'bytes':size,'reason':'package under 1 KiB; requires engine load check'})
    if pointer:
        expected_hash=pointer[1].decode();expected_size=int(pointer[2]);report['declared_lfs_bytes']+=expected_size
        if size!=expected_size:report['errors'].append({'path':name,'error':'LFS size mismatch','actual':size,'expected':expected_size})
        if name in changed:
            digest=hashlib.sha256()
            with target.open('rb') as stream:
                while block:=stream.read(1024*1024):digest.update(block)
            report['hashed_changed_packages']+=1
            if digest.hexdigest()!=expected_hash:report['errors'].append({'path':name,'error':'LFS SHA256 mismatch'})
    elif name in changed:report['suspicious'].append({'path':name,'reason':'changed package stored as non-LFS blob','git_blob_bytes':len(blob)})
proc.stdin.close();proc.wait()
report['smallest']=[{'path':n,'bytes':s} for s,n in sorted(sizes)[:30]]
report['passed']=not report['errors'];report['finished_at_epoch']=time.time()
(OUT/'package_file_audit.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
print(json.dumps({k:v for k,v in report.items() if k!='smallest'},ensure_ascii=False,indent=2))
