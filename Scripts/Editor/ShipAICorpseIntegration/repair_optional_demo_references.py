"""Explicit canonical defaults where the original demo resource never existed in Git."""
import json,shutil
from pathlib import Path
import unreal
ROOT=Path(unreal.Paths.project_dir()).resolve();OUT=ROOT/'Saved/AssetRepair'
fallbacks=[
 {'from':'/Game/JoseonTongsinsaseon/Textures/Landscape/T_MacroVariation_BC','to':'/Engine/EngineMaterials/T_Default_MacroVariation','reason':'original macro variation image absent from all Git history and local sources; explicit stock macro-noise default'},
 {'from':'/Game/DemoTemplate/_Core/MI_Intro_Colorway','to':'/Game/LevelPrototyping/Materials/MI_DefaultColorway','reason':'retired imported demo colorway absent; canonical current template colorway material'}]
for row in fallbacks:
    asset=unreal.load_asset(row['to']);assert asset,row
    assert unreal.SWAssetRepairLibrary.create_historical_redirector(row['from'],asset),row
    assert unreal.load_asset(row['from'])==asset,row
maps=sorted({r['from'] for r in json.loads((ROOT/'Saved/ShipAICorpseIntegration/engine_asset_audit.json').read_text(encoding='utf8'))['missing_hard_references'] if r['to'].endswith('_BuiltData')})
saved=[]
for path in maps:
    file=ROOT/'Content'/(path.removeprefix('/Game/')+'.umap')
    backup=OUT/'BeforeMapNormalize'/file.relative_to(ROOT);backup.parent.mkdir(parents=True,exist_ok=True)
    if not backup.exists():shutil.copy2(file,backup)
    world=unreal.load_asset(path);assert world and isinstance(world,unreal.World)
    assert unreal.EditorLoadingAndSavingUtils.save_map(world,path)
    saved.append(path)
(OUT/'optional_demo_repair.json').write_text(json.dumps({'passed':True,'canonical_defaults':fallbacks,'maps_resaved_without_missing_generated_lighting_data':saved,'limitation':'original demo art/baked lighting not recoverable; defaults are disclosed, not claimed as exact originals'},indent=2),encoding='utf8')
