import unreal,json,traceback
from pathlib import Path
root=Path(unreal.Paths.project_dir());base=root/'SourceArt/Characters/ChWoman';out=root/'Saved/ChWomanRigValidation';out.mkdir(parents=True,exist_ok=True)
report={};lib=unreal.SWCharacterRigValidationLibrary
try:
    unreal.SystemLibrary.execute_console_command(unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world(),'Interchange.FeatureFlags.Import.FBX 0')
    dest='/Game/jiwon/Characters/ChWomanUE5';options=unreal.FbxImportUI()
    for prop,val in [('import_mesh',True),('import_as_skeletal',True),('mesh_type_to_import',unreal.FBXImportType.FBXIT_SKELETAL_MESH),('automated_import_should_detect_type',False),('import_animations',False),('import_materials',False),('import_textures',False),('create_physics_asset',False)]:options.set_editor_property(prop,val)
    skeleton=unreal.load_asset(dest+'/SKM_ChWoman_UE5_Skeleton')
    if skeleton:options.set_editor_property('skeleton',skeleton)
    task=unreal.AssetImportTask();task.filename=str(base/'FBX/SKM_ChWoman_UE5.fbx');task.destination_path=dest;task.destination_name='SKM_ChWoman_UE5';task.automated=True;task.replace_existing=True;task.options=options;task.factory=unreal.FbxFactory();task.save=False
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    mesh=unreal.load_asset(dest+'/SKM_ChWoman_UE5');assert mesh
    materials={s.material_slot_name:s.material_interface for s in unreal.load_asset('/Game/jiwon/Characters/SKM_Player_Woman').get_editor_property('materials')}
    slots=mesh.get_editor_property('materials')
    for s in slots:
        assert s.material_slot_name in materials;s.material_interface=materials[s.material_slot_name]
    mesh.set_editor_property('materials',slots);skeleton=mesh.get_editor_property('skeleton')
    skeleton.set_editor_property('compatible_skeletons',[unreal.load_asset('/Game/Characters/Mannequins/Meshes/SK_Mannequin')]);skeleton.set_editor_property('use_retarget_modes_from_compatible_skeleton',True)
    assert lib.ensure_rig_physics(mesh)
    for asset in [mesh,skeleton,mesh.get_editor_property('physics_asset')]:assert unreal.EditorAssetLibrary.save_loaded_asset(asset,False)
    abp=unreal.load_asset('/Game/Anim_Logic/ABP_Player_Woman');path='/Game/Developers/ChWomanRigValidation/ABP_Woman_NoFixedRigOffsets'
    candidate=unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else unreal.EditorAssetLibrary.duplicate_asset(abp.get_path_name(),path)
    graph=json.loads(lib.describe_graph(candidate))
    if any(n['class']=='AnimGraphNode_ModifyBone' for n in graph['nodes']):assert lib.bypass_fixed_bone_corrections(candidate,['AnimGraphNode_ModifyBone_0','AnimGraphNode_ModifyBone_1'])
    assert unreal.EditorAssetLibrary.save_loaded_asset(candidate,False)
    report['prepared_new_mesh']=json.loads(lib.describe_mesh(mesh));report['candidate_graph']=json.loads(lib.describe_graph(candidate))
    task=unreal.AssetExportTask();task.object=mesh;task.filename=str(base/'Reference/Woman_UE_RoundTrip.fbx');task.automated=True;task.prompt=False;task.replace_identical=True;task.exporter=unreal.SkeletalMeshExporterFBX();task.options=unreal.FbxExportOption()
    report['roundtrip_exported']=unreal.Exporter.run_asset_export_task(task);report['passed']=True
except Exception:report['error']=traceback.format_exc();unreal.log_error(report['error'])
(out/'candidates.json').write_text(json.dumps(report,indent=2),encoding='utf8')
