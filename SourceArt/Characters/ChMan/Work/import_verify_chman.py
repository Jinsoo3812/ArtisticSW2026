import unreal, os, json, traceback
BASE = r'C:\Users\I\Documents\GitHub\ArtisticSW2026\SourceArt\Characters\ChMan'
report = {'stage':'start'}
try:
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    unreal.SystemLibrary.execute_console_command(world,'Interchange.FeatureFlags.Import.FBX 0')
    report['interchange_fbx_enabled']=unreal.SystemLibrary.get_console_variable_bool_value('Interchange.FeatureFlags.Import.FBX')
    dest = '/Game/jiwon/Characters/ChManUE5'
    original = unreal.load_asset('/Game/jiwon/Characters/ChManNewRig')
    opts = unreal.FbxImportUI()
    opts.set_editor_property('import_mesh',True)
    opts.set_editor_property('import_as_skeletal',True)
    opts.set_editor_property('mesh_type_to_import',unreal.FBXImportType.FBXIT_SKELETAL_MESH)
    opts.set_editor_property('automated_import_should_detect_type',False)
    opts.set_editor_property('import_animations',False)
    opts.set_editor_property('import_materials',False)
    opts.set_editor_property('import_textures',False)
    opts.set_editor_property('create_physics_asset',False)
    opts.skeletal_mesh_import_data.set_editor_property('use_t0_as_ref_pose',False)
    opts.skeletal_mesh_import_data.set_editor_property('update_skeleton_reference_pose',True)
    own_skeleton = unreal.load_asset(dest+'/SKM_ChMan_UE5_Skeleton')
    if own_skeleton:
        opts.set_editor_property('skeleton',own_skeleton)
    task = unreal.AssetImportTask()
    task.set_editor_property('filename',BASE+r'\FBX\SKM_ChMan_UE5.fbx')
    task.set_editor_property('destination_path',dest)
    task.set_editor_property('destination_name','SKM_ChMan_UE5')
    task.set_editor_property('automated',True)
    task.set_editor_property('replace_existing',True)
    task.set_editor_property('replace_existing_settings',True)
    task.set_editor_property('save',False)
    task.set_editor_property('factory',unreal.FbxFactory())
    task.set_editor_property('options',opts)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    report['imported_paths'] = list(task.imported_object_paths)
    mesh = unreal.load_asset(dest+'/SKM_ChMan_UE5')
    if not mesh:raise RuntimeError('Skeletal mesh import failed')
    slots = list(mesh.get_editor_property('materials'))
    source = {str(s.material_slot_name):s.material_interface for s in original.get_editor_property('materials')}
    report['original_materials']={k:v.get_path_name() if v else None for k,v in source.items()}
    for slot in slots:
        name = str(slot.material_slot_name)
        if name in source:slot.material_interface=source[name]
    mesh.set_editor_property('materials',slots)
    skeleton=mesh.get_editor_property('skeleton')
    report['skeleton']=skeleton.get_path_name()
    report['materials']=[{'slot':str(s.material_slot_name),'material':s.material_interface.get_path_name() if s.material_interface else None} for s in slots]
    try:
        skeleton.set_editor_property('compatible_skeletons',[unreal.load_asset('/Game/Characters/Mannequins/Meshes/SK_Mannequin')])
        skeleton.set_editor_property('use_retarget_modes_from_compatible_skeleton',True)
        report['compatible_skeleton_set']=True
    except Exception as e:report['compatible_error']=str(e)
    unreal.EditorAssetLibrary.save_loaded_asset(skeleton)
    unreal.EditorAssetLibrary.save_loaded_asset(mesh)
    report['saved']=True
    report['docs']={name:str(getattr(unreal,name).__doc__) for name in ['AnimPoseEvaluationOptions','AnimPoseExtensions','AnimSequenceExporterFBX'] if hasattr(unreal,name)}
    if hasattr(unreal,'AnimPoseExtensions'):
        report['pose_methods']={n:str(getattr(unreal.AnimPoseExtensions,n).__doc__) for n in dir(unreal.AnimPoseExtensions) if n.startswith('get_')}
    clips={'Idle':'/Game/Characters/Mannequins/Anims/Unarmed/MM_Idle',
           'Walk':'/Game/Characters/Mannequins/Anims/Unarmed/Walk/MF_Unarmed_Walk_Fwd',
           'Jump':'/Game/Characters/Mannequins/Anims/Unarmed/Jump/MM_Jump'}
    report['animations']={}
    for label,path in clips.items():
        anim=unreal.load_asset(path)
        if not anim:continue
        entry={'asset':path,'class':anim.get_class().get_name(),'skeleton':anim.get_editor_property('skeleton').get_path_name()}
        report['animations'][label]=entry
        if hasattr(unreal,'AnimSequenceExporterFBX') and isinstance(anim,unreal.AnimSequence):
            task=unreal.AssetExportTask();task.object=anim
            task.filename=BASE+'\\Reference\\AS_'+label+'.fbx'
            task.automated=True;task.prompt=False;task.replace_identical=True
            task.exporter=unreal.AnimSequenceExporterFBX();task.options=unreal.FbxExportOption()
            entry['exported']=unreal.Exporter.run_asset_export_task(task)
    # Return the real UE importer result through a second FBX round trip.
    task=unreal.AssetExportTask();task.object=mesh;task.filename=BASE+'\\Reference\\ChMan_UE5_UE_RoundTrip.fbx'
    task.automated=True;task.prompt=False;task.replace_identical=True
    task.exporter=unreal.SkeletalMeshExporterFBX();task.options=unreal.FbxExportOption()
    report['roundtrip_exported']=unreal.Exporter.run_asset_export_task(task)
    report['stage']='complete'
except Exception:
    report['error']=traceback.format_exc()
with open(BASE+'\\Work\\unreal_import_audit.json','w',encoding='utf8') as f:json.dump(report,f,indent=2,ensure_ascii=False)
