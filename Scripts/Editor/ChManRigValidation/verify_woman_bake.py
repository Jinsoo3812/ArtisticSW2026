"""Native UE import/evaluation of the authoring rig's eight-frame demo export."""
import unreal,json,math
from pathlib import Path
base=Path(unreal.Paths.project_dir())/'SourceArt/Characters/ChWoman';out=Path(unreal.Paths.project_saved_dir())/'ChWomanRigValidation'
mesh=unreal.load_asset('/Game/jiwon/Characters/ChWomanUE5/SKM_ChWoman_UE5')
unreal.SystemLibrary.execute_console_command(unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world(),'Interchange.FeatureFlags.Import.FBX 0')
options=unreal.FbxImportUI()
for p,v in [('import_mesh',False),('import_as_skeletal',True),('mesh_type_to_import',unreal.FBXImportType.FBXIT_ANIMATION),('automated_import_should_detect_type',False),('import_animations',True),('import_materials',False),('import_textures',False),('skeleton',mesh.get_editor_property('skeleton'))]:options.set_editor_property(p,v)
task=unreal.AssetImportTask();task.filename=str(base/'FBX/AS_ChWoman_Baked.fbx');task.destination_path='/Game/Developers/ChWomanRigValidation';task.destination_name='AS_ControlRig_Bake_Validation';task.automated=True;task.replace_existing=True;task.options=options;task.factory=unreal.FbxFactory();task.save=False
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
anim=next(unreal.load_asset(path) for path in task.imported_object_paths if isinstance(unreal.load_asset(path),unreal.AnimSequence))
expected=json.loads((base/'Work/baked_expected.json').read_text(encoding='utf8'));report={'asset':anim.get_path_name(),'duration':anim.get_play_length(),'samples':[]}
metadata=json.loads((base/'FBX/AS_ChWoman_Baked.export.json').read_text(encoding='utf8'));fps=metadata['fps']/metadata['fps_base'];report['source_fps']=fps
for frame,bones in expected.items():
    opts=unreal.AnimPoseEvaluationOptions();opts.set_editor_property('optional_skeletal_mesh',mesh);opts.set_editor_property('should_retarget',False)
    pose=unreal.AnimPoseExtensions.get_anim_pose_at_time(anim,min(anim.get_play_length(),(int(frame)-metadata['frame_start'])/fps),opts);errors={}
    for name,rows in bones.items():
        t=unreal.AnimPoseExtensions.get_bone_pose(pose,name,unreal.AnimPoseSpaces.WORLD).translation
        target=[rows[0][3]*100,-rows[1][3]*100,rows[2][3]*100]
        errors[name]=math.sqrt(sum((a-b)**2 for a,b in zip([t.x,t.y,t.z],target)))
    worst=max(errors,key=errors.get);report['samples'].append({'frame':int(frame),'worst_bone':worst,'max_head_error_cm':errors[worst]})
report['passed']=max(s['max_head_error_cm'] for s in report['samples'])<.01
(out/'native_bake_audit.json').write_text(json.dumps(report,indent=2),encoding='utf8');assert report['passed'],report
