"""Import the sash as an isolated NPC skeleton/mesh and configure owned clips."""
import json
from pathlib import Path
import unreal
ROOT=Path(unreal.Paths.project_dir()).resolve();ART=ROOT/'SourceArt/Characters/YiSunSin'
BASE='/Game/Chae_Asset/YiSunSinContent';NPC=BASE+'/NPC'
unreal.SystemLibrary.execute_console_command(None,'Interchange.FeatureFlags.Import.FBX 0')
source=unreal.load_asset(BASE+'/Characters/SK_YiSunSin_Latest')
old=unreal.load_asset(NPC+'/Characters/SK_YiSunSin_NPC_Sash')
options=unreal.FbxImportUI()
for n,v in [('automated_import_should_detect_type',False),('import_as_skeletal',True),('import_mesh',True),('import_animations',False),('import_materials',False),('import_textures',False),('create_physics_asset',False)]:options.set_editor_property(n,v)
options.set_editor_property('mesh_type_to_import',unreal.FBXImportType.FBXIT_SKELETAL_MESH)
if old:
    assert old.get_editor_property('skeleton')!=source.get_editor_property('skeleton')
    options.set_editor_property('skeleton',old.get_editor_property('skeleton'))
data=options.get_editor_property('skeletal_mesh_import_data')
# This is the private NPC skeleton; updating its four helper bind transforms
# must never target the original shared skeleton. Original 83 are checked below.
data.set_editor_property('update_skeleton_reference_pose',True);data.set_editor_property('use_t0_as_ref_pose',False)
data.set_editor_property('normal_import_method',unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS_AND_TANGENTS)
def do_import(filename,name,destination,options):
    task=unreal.AssetImportTask();task.filename=str(ART/'FBX'/filename);task.destination_name=name;task.destination_path=destination
    task.automated=True;task.replace_existing=True;task.save=False;task.factory=unreal.FbxFactory();task.options=options
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    return task.get_objects()
mesh=next(o for o in do_import('SK_YiSunSin_NPC_Sash.fbx','SK_YiSunSin_NPC_Sash',NPC+'/Characters',options) if isinstance(o,unreal.SkeletalMesh))
skeleton=mesh.get_editor_property('skeleton')
assert skeleton!=source.get_editor_property('skeleton'),'Original skeleton must never receive NPC sash bones'
component=unreal.SkeletalMeshComponent();component.set_skeletal_mesh_asset(mesh)
source_component=unreal.SkeletalMeshComponent();source_component.set_skeletal_mesh_asset(source)
assert component.get_num_bones()==87 and source_component.get_num_bones()==83
source_names=[str(source_component.get_bone_name(i)) for i in range(83)]
npc_names=[str(component.get_bone_name(i)) for i in range(87)]
assert set(npc_names)-set(source_names)=={'npc_sash_%02d'%i for i in range(1,5)}
for i,name in enumerate(source_names):
    index=component.get_bone_index(name)
    assert component.get_parent_bone(name)==source_component.get_parent_bone(name)
    a=component.get_ref_pose_transform(index);b=source_component.get_ref_pose_transform(i)
    assert (a.translation-b.translation).length()<.002
    assert max(abs(x-y) for x,y in zip([a.scale3d.x,a.scale3d.y,a.scale3d.z],[b.scale3d.x,b.scale3d.y,b.scale3d.z]))<.0001
mesh.set_editor_property('materials',source.get_editor_property('materials'))
mesh.set_editor_property('physics_asset',source.get_editor_property('physics_asset'))
unreal.EditorAssetLibrary.save_loaded_asset(skeleton,False);unreal.EditorAssetLibrary.save_loaded_asset(mesh,False)
animations=[]
for label,frames in [('Idle_Upright',122),('Walk',36),('Run',26)]:
    opts=unreal.FbxImportUI()
    for n,v in [('automated_import_should_detect_type',False),('import_mesh',False),('import_as_skeletal',True),('import_animations',True),('import_materials',False),('import_textures',False)]:opts.set_editor_property(n,v)
    opts.set_editor_property('mesh_type_to_import',unreal.FBXImportType.FBXIT_ANIMATION);opts.set_editor_property('skeleton',skeleton)
    anim_data=opts.get_editor_property('anim_sequence_import_data');anim_data.set_editor_property('remove_redundant_keys',False)
    anim_data.set_editor_property('animation_length',unreal.FBXAnimationLengthImportType.FBXALIT_EXPORTED_TIME)
    name='AS_YiSunSin_NPC_Sash_'+label
    animation=next(o for o in do_import(name+'.fbx',name,NPC+'/Animations',opts) if isinstance(o,unreal.AnimSequence))
    root_keys=[];bind=component.get_ref_pose_transform(0)
    for frame in range(frames):
        pose=unreal.AnimPoseExtensions.get_anim_pose_at_time(animation,frame/25.0,unreal.AnimPoseEvaluationOptions())
        local=unreal.AnimPoseExtensions.get_bone_pose(pose,'spine',unreal.AnimPoseSpaces.LOCAL)
        assert abs(local.scale3d.x-1)<.001
        root_keys.append(unreal.MathLibrary.compose_transforms(local,bind))
    assert animation.controller.set_bone_track_keys('spine',[k.translation for k in root_keys],[k.rotation for k in root_keys],[k.scale3d for k in root_keys])
    if label in ['Walk','Run']:
        # FBX export includes a padding sample. Preserve the original UE timing
        # and original body tracks exactly; transfer only the four sash tracks.
        original_animation=unreal.load_asset(BASE+'/Animations/Latest/AS_YiSunSin_'+label)
        original_frames=unreal.AnimationLibrary.get_num_frames(original_animation)
        original_length=original_animation.get_play_length()
        source_rate=round(original_frames/original_length)
        sash_length=animation.get_play_length()
        body_keys={name:[] for name in source_names}
        sash_keys={'npc_sash_%02d'%i:[] for i in range(1,5)}
        for frame in range(original_frames+1):
            body_pose=unreal.AnimPoseExtensions.get_anim_pose_at_time(original_animation,frame/source_rate,unreal.AnimPoseEvaluationOptions())
            sash_pose=unreal.AnimPoseExtensions.get_anim_pose_at_time(animation,sash_length*frame/original_frames,unreal.AnimPoseEvaluationOptions())
            for name in body_keys:body_keys[name].append(unreal.AnimPoseExtensions.get_bone_pose(body_pose,name,unreal.AnimPoseSpaces.LOCAL))
            for name in sash_keys:sash_keys[name].append(unreal.AnimPoseExtensions.get_bone_pose(sash_pose,name,unreal.AnimPoseSpaces.LOCAL))
        animation.controller.set_frame_rate(unreal.FrameRate(numerator=source_rate,denominator=1))
        animation.controller.set_number_of_frames(unreal.FrameNumber(value=original_frames))
        for name,keys in {**body_keys,**sash_keys}.items():
            assert animation.controller.set_bone_track_keys(name,[k.translation for k in keys],[k.rotation for k in keys],[k.scale3d for k in keys])
    animation.set_editor_property('enable_root_motion',False);animation.set_editor_property('force_root_lock',False)
    unreal.EditorAssetLibrary.save_loaded_asset(animation,False);animations.append(animation)
blend=unreal.load_asset(NPC+'/Animations/BS_YiSunSin_NPC_Sash')
if not blend:
    factory=unreal.BlendSpaceFactory1D();factory.set_editor_property('target_skeleton',skeleton);factory.set_editor_property('preview_skeletal_mesh',mesh)
    blend=unreal.AssetToolsHelpers.get_asset_tools().create_asset('BS_YiSunSin_NPC_Sash',NPC+'/Animations',unreal.BlendSpace1D,factory)
params=list(blend.get_editor_property('blend_parameters'));params[0].set_editor_property('display_name','Speed');params[0].set_editor_property('min',0.0);params[0].set_editor_property('max',450.0);blend.set_editor_property('blend_parameters',params)
samples=[]
for animation,speed in zip(animations,[0,150,450]):
    sample=unreal.BlendSample();sample.set_editor_property('animation',animation);sample.set_editor_property('sample_value',unreal.Vector(speed,0,0));samples.append(sample)
blend.set_editor_property('sample_data',samples);unreal.SWFoleyMigrationLibrary.rebuild_contact_blend_space(blend);unreal.EditorAssetLibrary.save_loaded_asset(blend,False)
abp=unreal.load_asset(NPC+'/Animations/ABP_YiSunSin_NPC_Sash') or unreal.EditorAssetLibrary.duplicate_asset(NPC+'/Animations/ABP_YiSunSin_NPC',NPC+'/Animations/ABP_YiSunSin_NPC_Sash')
abp.set_editor_property('target_skeleton',skeleton)
graph=unreal.BlueprintEditorLibrary.find_graph(abp,'AnimGraph')
for player in graph.get_graph_nodes_of_class(unreal.AnimGraphNode_BlendSpacePlayer):
    node=player.get_editor_property('node');node.set_editor_property('blend_space',blend);player.set_editor_property('node',node)
assert unreal.SWNpcAuthoringLibrary.configure_npc_locomotion_graph(abp,skeleton,blend)
unreal.EditorAssetLibrary.save_loaded_asset(abp,False)
bp=unreal.load_asset(NPC+'/Blueprints/BP_YiSunSin_NPC');cdo=unreal.get_default_object(bp.generated_class())
cdo.get_editor_property('mesh').set_skeletal_mesh_asset(mesh);cdo.get_editor_property('mesh').set_anim_instance_class(abp.generated_class())
unreal.EditorAssetLibrary.save_loaded_asset(bp,False)
# Re-instance the comparison actor so its inherited mesh/ABP display the new variant.
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
level.load_level(NPC+'/Preview/L_YiSunSin_NPC_Preview')
for actor in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
    if actor.get_class()==bp.generated_class():
        actor.get_editor_property('mesh').set_skeletal_mesh_asset(mesh);actor.get_editor_property('mesh').set_anim_instance_class(abp.generated_class())
        actor.get_editor_property('mesh').set_update_animation_in_editor(True)
level.save_current_level()
report={'npc_mesh':mesh.get_path_name(),'npc_skeleton':skeleton.get_path_name(),'original_bones_unchanged':83,'npc_bones':87,'blend_space':blend.get_path_name(),'animation_blueprint':abp.get_path_name(),'clips':[a.get_path_name() for a in animations]}
(ART/'Audit/sash_ue_setup.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
unreal.get_editor_subsystem(unreal.AssetEditorSubsystem).open_editor_for_assets([blend])
unreal.log('NPC_SASH_SETUP_COMPLETE')
