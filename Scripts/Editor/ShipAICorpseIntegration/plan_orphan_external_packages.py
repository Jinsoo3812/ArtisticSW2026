"""Prove missing-world external packages are unreachable before any relocation."""
import json
from pathlib import Path
import unreal
ROOT=Path(unreal.Paths.project_dir()).resolve();OUT=ROOT/'Saved/AssetRepair'
REG=unreal.AssetRegistryHelpers.get_asset_registry();REG.search_all_assets(True)
missing=json.loads((ROOT/'Saved/ShipAICorpseIntegration/engine_asset_audit.json').read_text(encoding='utf8'))['missing_hard_references']
roots=set()
for row in missing:
    source=row['from']
    if '/__ExternalActors__/' not in source and '/__ExternalObjects__/' not in source:continue
    pieces=source.split('/');owner='/Game/'+'/'.join(pieces[3:-3])
    if not (ROOT/'Content'/(owner.removeprefix('/Game/')+'.umap')).exists():roots.add('/'.join(pieces[:-3]))
packages={str(a.package_name) for root in roots for a in REG.get_assets_by_path(root,recursive=True)}
options=unreal.AssetRegistryDependencyOptions(include_hard_package_references=True,include_soft_package_references=True,include_searchable_names=True,include_soft_management_references=True,include_hard_management_references=True)
outside=[]
for package in packages:
    for ref in REG.get_referencers(package,options) or []:
        if str(ref) not in packages:outside.append({'target':package,'from':str(ref)})
report={'roots':sorted(roots),'packages':sorted(packages),'outside_referencers':outside,'safe_to_archive':not outside}
(OUT/'orphan_plan.json').write_text(json.dumps(report,indent=2),encoding='utf8')
assert not outside,outside
unreal.log('ORPHAN_PLAN roots='+str(len(roots))+' packages='+str(len(packages)))
