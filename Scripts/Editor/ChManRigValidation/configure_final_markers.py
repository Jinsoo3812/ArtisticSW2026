import unreal,json,traceback
from pathlib import Path
OUT=Path(unreal.Paths.project_saved_dir())/'ChManRigValidation';report={}
try:
    path='/Game/jiwon/Characters/ChManUE5/ABP_ChMan_FinalMarkers'
    mesh=unreal.load_asset('/Game/jiwon/Characters/ChManUE5/SKM_ChMan_UE5')
    bp=unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
    if not bp:
        factory=unreal.AnimBlueprintFactory()
        factory.set_editor_property('target_skeleton',mesh.get_editor_property('skeleton'))
        factory.set_editor_property('parent_class',unreal.AnimInstance)
        bp=unreal.AssetToolsHelpers.get_asset_tools().create_asset('ABP_ChMan_FinalMarkers','/Game/jiwon/Characters/ChManUE5',unreal.AnimBlueprint,factory)
    assert bp
    assert unreal.SWCharacterRigValidationLibrary.configure_final_limb_markers(bp,mesh),'Final-marker graph failed validation or compile'
    assert unreal.EditorAssetLibrary.save_loaded_asset(bp,False)
    assert unreal.EditorAssetLibrary.save_loaded_asset(mesh,False)
    report['graph']=json.loads(unreal.SWCharacterRigValidationLibrary.describe_graph(bp))
    report['mesh']=mesh.get_path_name();report['applied']=True
except Exception:
    report['error']=traceback.format_exc();unreal.log_error(report['error'])
(OUT/'final_markers.json').write_text(json.dumps(report,indent=2),encoding='utf8')
