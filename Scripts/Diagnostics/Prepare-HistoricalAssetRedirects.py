"""Plan canonical aliases using the documented folder moves; preserve restored originals."""
import hashlib,json,shutil,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'Saved/AssetRepair'
rows=json.loads((OUT/'historical_restoration.json').read_text(encoding='utf8'))['restored']
aliases=[]
for row in rows:
    if row['package'].startswith('/Game/Blueprints/Item/'):
        aliases.append({'from':row['package'],'to':row['package'].replace('/Game/Blueprints/Item/','/Game/Blueprints/Item/Legacy/',1),'evidence':'7f3b3676 folder migration; preserved current Legacy authoring'})
aliases += [
 {'from':'/Game/New/Cannon/BP_Cannon','to':'/Game/Blueprints/Ship/Cannon/BP_Cannon','evidence':'455ca9f1 folder reorganization'},
 {'from':'/Game/New/Enemy_Ship/BP_EnemyShip','to':'/Game/Blueprints/Ship/Enemy_Ship/Blueprints/BP_EnemyShip','evidence':'455ca9f1 folder reorganization and historical intermediate redirect'},
 {'from':'/Game/New/Ship/BP_TestShip_SingleMesh','to':'/Game/Blueprints/Ship/Legacy/BP_TestShip_SingleMesh','evidence':'455ca9f1 folder reorganization'},
 {'from':'/Game/New/Water/Water_Material_Ocean_Custom','to':'/Game/Blueprints/Water/Legacy/Water_Material_Ocean_Custom','evidence':'455ca9f1 folder reorganization'},
 {'from':'/Game/LevelPrototyping/Interactable/Door/Assets/Meshes/SM_DoorFrame_Edge','to':'/Game/LevelPrototyping/Interactable/Door/Meshes/SM_DoorFrame_Edge','evidence':'canonical existing same-name mesh in moved Meshes folder'},
 {'from':'/Game/EngineSky/BP_Sky_Sphere','to':'/Engine/EngineSky/BP_Sky_Sphere','evidence':'original stock engine sky asset, same object and class'},
 {'from':'/Game/EngineSky/M_Sky_Panning_Clouds2','to':'/Engine/EngineSky/M_Sky_Panning_Clouds2','evidence':'original stock engine sky material, same object and class'}]
for alias in aliases:
    path=ROOT/('Content/'+alias['from'].removeprefix('/Game/')+'.uasset')
    if path.exists():
        row=next(r for r in rows if r['package']==alias['from'])
        assert path.stat().st_size==row['bytes']
        expected=row.get('lfs_sha256')
        if expected:assert hashlib.sha256(path.read_bytes()).hexdigest()==expected
        backup=OUT/'RecoveredOriginals'/path.relative_to(ROOT);backup.parent.mkdir(parents=True,exist_ok=True)
        shutil.copy2(path,backup)
        # Only files restored by our manifest were absent at task start.
        assert not subprocess.check_output(['git','ls-files','--',str(path.relative_to(ROOT))],cwd=ROOT)
        path.unlink()
subprocess.run(['git','restore','--source=a5d4c2bb4','--worktree','--','Content/New/Ship/Mesh/Test/SM_TestShip.uasset'],cwd=ROOT,check=True)
(OUT/'canonical_redirect_plan.json').write_text(json.dumps(aliases,ensure_ascii=False,indent=2),encoding='utf8')
print('Prepared canonical aliases:',len(aliases),'and recovered original 4.8 MB test hull behind the old redirector')
