"""Read-only registry/reference audit and engine loading of changed packages."""
import json,subprocess,traceback
from pathlib import Path
import unreal
ROOT=Path(unreal.Paths.project_dir()).resolve();OUT=ROOT/'Saved/ShipAICorpseIntegration'
REG=unreal.AssetRegistryHelpers.get_asset_registry();REG.search_all_assets(True)
report={'registry_packages':0,'missing_hard_references':[],'loaded_changed':[],'load_failures':[],'errors':[]}
options=unreal.AssetRegistryDependencyOptions(include_hard_package_references=True,include_soft_package_references=False,include_searchable_names=False,include_soft_management_references=False,include_hard_management_references=False)
try:
    catalog=REG.get_assets_by_path('/Game',recursive=True);packages={str(a.package_name) for a in catalog}
    report['registry_packages']=len(packages)
    file_packages={'/Game/'+str(p.relative_to(ROOT/'Content').with_suffix('')).replace('\\','/') for ext in ['*.uasset','*.umap'] for p in (ROOT/'Content').rglob(ext)}
    for package in sorted(packages):
        for dependency in REG.get_dependencies(package,options) or []:
            dep=str(dependency)
            if dep.startswith('/Game/') and dep not in packages and dep not in file_packages:
                report['missing_hard_references'].append({'from':package,'to':dep})
    changed=subprocess.check_output(['git','diff','--name-only','1c41c6e0','--','Content'],cwd=ROOT).decode('utf8').splitlines()
    critical=['Content/Developers/PlayerRagdoll/L_PlayerRagdollTest.umap','Content/Blueprints/Player/BP_Player_Man.uasset','Content/Blueprints/Player/BP_Player_Woman.uasset','Content/Anim_Logic/ABP_Player_Man.uasset','Content/Anim_Logic/ABP_Player_Woman.uasset']
    for index,path in enumerate(sorted(set(changed+critical))):
        if not path.endswith(('.uasset','.umap')) or not (ROOT/path).is_file():continue
        package='/Game/'+path.removeprefix('Content/').rsplit('.',1)[0]
        rows=REG.get_assets_by_package_name(package)
        if not rows:report['load_failures'].append({'path':package,'error':'not in asset registry'});continue
        for row in rows:
            asset=row.get_asset()
            if not asset:report['load_failures'].append({'path':package,'error':'GetAsset returned null'})
            else:report['loaded_changed'].append({'path':asset.get_path_name(),'class':asset.get_class().get_name()})
        asset=None;rows=None
        if index%8==0:unreal.SystemLibrary.collect_garbage()
    report['passed']=not report['errors'] and not report['load_failures']
except Exception:report['errors'].append(traceback.format_exc());unreal.log_error(report['errors'][-1])
(OUT/'engine_asset_audit.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
unreal.log('SHIP_AI_ASSET_AUDIT '+json.dumps({k:len(v) if isinstance(v,list) else v for k,v in report.items()}))
assert report.get('passed'),report['errors']+report['load_failures']
