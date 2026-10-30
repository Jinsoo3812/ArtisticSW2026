"""Import the NPC copy only after the unchanged FBX control preserves the UE rig."""
import json
from pathlib import Path
import unreal

ROOT = Path(unreal.Paths.project_dir()).resolve()
ART = ROOT / 'SourceArt/Characters/YiSunSin'
BASE = '/Game/Chae_Asset/YiSunSinContent'
DEST = BASE + '/NPC/Characters'
source = unreal.load_asset(BASE + '/Characters/SK_YiSunSin_Latest')
source_skeleton = source.get_editor_property('skeleton')
unreal.SystemLibrary.execute_console_command(None, 'Interchange.FeatureFlags.Import.FBX 0')

def inspect_mesh(mesh):
    component = unreal.SkeletalMeshComponent()
    component.set_skeletal_mesh_asset(mesh)
    bones = []
    for index in range(component.get_num_bones()):
        name = component.get_bone_name(index)
        transform = component.get_ref_pose_transform(index)
        bones.append({'name': str(name), 'parent': str(component.get_parent_bone(name)),
                      'translation': [transform.translation.x, transform.translation.y, transform.translation.z],
                      'rotation': [transform.rotation.x, transform.rotation.y, transform.rotation.z, transform.rotation.w],
                      'scale': [transform.scale3d.x, transform.scale3d.y, transform.scale3d.z]})
    return bones

def import_mesh(filename, name, skeleton=None):
    options = unreal.FbxImportUI()
    options.set_editor_property('automated_import_should_detect_type', False)
    options.set_editor_property('mesh_type_to_import', unreal.FBXImportType.FBXIT_SKELETAL_MESH)
    options.set_editor_property('import_as_skeletal', True)
    options.set_editor_property('import_mesh', True)
    options.set_editor_property('import_animations', False)
    options.set_editor_property('import_materials', False)
    options.set_editor_property('import_textures', False)
    options.set_editor_property('create_physics_asset', False)
    if skeleton:
        options.set_editor_property('skeleton', skeleton)
    data = options.get_editor_property('skeletal_mesh_import_data')
    data.set_editor_property('update_skeleton_reference_pose', False)
    data.set_editor_property('use_t0_as_ref_pose', False)
    data.set_editor_property('import_mesh_lods', False)
    data.set_editor_property('normal_import_method', unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS_AND_TANGENTS)
    task = unreal.AssetImportTask()
    task.filename = str(ART / 'FBX' / filename)
    task.destination_path = DEST
    task.destination_name = name
    task.automated = True
    task.replace_existing = True
    task.save = False
    task.options = options
    task.factory = unreal.FbxFactory()
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    meshes = [obj for obj in task.get_objects() if isinstance(obj, unreal.SkeletalMesh)]
    if len(meshes) != 1:
        raise RuntimeError('Expected one imported mesh: ' + str(task.imported_object_paths))
    return meshes[0]

def compare_rig(expected, actual):
    if [(b['name'], b['parent']) for b in expected] != [(b['name'], b['parent']) for b in actual]:
        raise RuntimeError('Bone names/order/parents changed')
    translation = max(sum((a-b)**2 for a,b in zip(x['translation'],y['translation']))**0.5
                      for x,y in zip(expected,actual))
    scale = max(abs(a-b) for x,y in zip(expected,actual) for a,b in zip(x['scale'],y['scale']))
    quaternion = max(min(sum((a-b)**2 for a,b in zip(x['rotation'],y['rotation']))**0.5,
                         sum((a+b)**2 for a,b in zip(x['rotation'],y['rotation']))**0.5)
                     for x,y in zip(expected,actual))
    values = {'translation_error_cm': translation, 'scale_error': scale, 'quaternion_error': quaternion}
    if translation > 0.002 or scale > 0.0001 or quaternion > 0.0001:
        raise RuntimeError('FBX reference pose drift: ' + json.dumps(values))
    return values

original_bones = inspect_mesh(source)
control = import_mesh('SK_YiSunSin_UnchangedControl.fbx', 'SK_YiSunSin_RoundTripControl')
report = {'engine': unreal.SystemLibrary.get_engine_version(), 'source_bones': original_bones,
          'control_bones': inspect_mesh(control), 'importer': 'legacy FBX, task-scoped factory; session CVar'}
(ART / 'Audit/ue_import.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
report['control_rig_comparison'] = compare_rig(original_bones, report['control_bones'])
(ART / 'Audit/ue_import.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
candidate = import_mesh('SK_YiSunSin_NPC.fbx', 'SK_YiSunSin_NPC', source_skeleton)
report['candidate_bones'] = inspect_mesh(candidate)
report['candidate_rig_comparison'] = compare_rig(original_bones, report['candidate_bones'])
candidate.set_editor_property('materials', source.get_editor_property('materials'))
candidate.set_editor_property('physics_asset', source.get_editor_property('physics_asset'))
candidate.set_editor_property('post_process_anim_blueprint', source.get_editor_property('post_process_anim_blueprint'))
unreal.EditorAssetLibrary.save_loaded_asset(candidate, only_if_is_dirty=False)
report['saved_mesh'] = candidate.get_path_name()
report['shared_original_skeleton'] = candidate.get_editor_property('skeleton').get_path_name()
task = unreal.AssetExportTask()
task.object = candidate
task.exporter = unreal.SkeletalMeshExporterFBX()
task.filename = str(ART / 'Reference/SK_YiSunSin_NPC_UE_RoundTrip.fbx')
task.automated = True
task.prompt = False
task.replace_identical = True
task.options = unreal.FbxExportOption()
task.options.set_editor_property('bake_material_inputs', unreal.FbxMaterialBakeMode.DISABLED)
task.options.set_editor_property('level_of_detail', False)
if not unreal.Exporter.run_asset_export_task(task):
    raise RuntimeError('Could not export imported NPC for geometry verification')
(ART / 'Audit/ue_import.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
unreal.log('YISUNSIN_NPC_IMPORT_COMPLETE')
