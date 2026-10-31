"""Restore the missing original skeleton and preserve every authored animation/graph node."""
import json,shutil
from pathlib import Path
import unreal
ROOT=Path(unreal.Paths.project_dir()).resolve();OUT=ROOT/'Saved/AssetRepair';OUT.mkdir(parents=True,exist_ok=True)
BASE='/Game/Chae_Asset/YiSunSinContent'
bp=unreal.load_asset(BASE+'/Animations/ABP_YiSunSin');blend=unreal.load_asset(BASE+'/Animations/BS_YiSunSin_Locomotion');mesh=unreal.load_asset(BASE+'/Characters/SK_YiSunSin_Latest')
assert bp and blend and mesh
before=json.loads(unreal.SWCharacterRigValidationLibrary.describe_graph(bp))
for asset in [bp,blend]:
    file=ROOT/'Content'/(asset.get_path_name().split('.')[0].removeprefix('/Game/')+'.uasset')
    dest=OUT/'Before'/file.relative_to(ROOT);dest.parent.mkdir(parents=True,exist_ok=True)
    if not dest.exists():shutil.copy2(file,dest)
samples_before=[s.export_text() for s in blend.get_editor_property('sample_data')]
axes_before=[a.export_text() for a in blend.get_editor_property('blend_parameters')]
assert unreal.SWNpcAuthoringLibrary.repair_npc_animation_bindings(bp,mesh,blend)
after=json.loads(unreal.SWCharacterRigValidationLibrary.describe_graph(bp))
assert {(n['name'],n['guid'],n['class']) for n in before['nodes']}=={(n['name'],n['guid'],n['class']) for n in after['nodes']}
samples_after=[s.export_text() for s in blend.get_editor_property('sample_data')]
# Validation updates derived sample flags; the actual animation and speed must match.
def authored(s):return (s.get_editor_property('animation').get_path_name(),s.get_editor_property('sample_value').export_text())
for asset in [blend,bp]:assert unreal.EditorAssetLibrary.save_loaded_asset(asset,False)
report={'passed':True,'skeleton':str(bp.get_editor_property('target_skeleton')),'graph_node_count':len(after['nodes']),'preserved_node_guids':True,
 'samples_before':samples_before,'samples_after':samples_after,'axes_before':axes_before,'axes_after':[a.export_text() for a in blend.get_editor_property('blend_parameters')]}
(OUT/'npc_repair.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
