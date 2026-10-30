"""Pre-sash baseline setup. Use setup_yisunsin_sash.py for the current NPC variant."""
import json
from pathlib import Path
import unreal

ROOT = Path(unreal.Paths.project_dir()).resolve()
ART = ROOT / 'SourceArt/Characters/YiSunSin'
BASE = '/Game/Chae_Asset/YiSunSinContent'
NPC = BASE + '/NPC'
report = {}
unreal.SystemLibrary.execute_console_command(None, 'Interchange.FeatureFlags.Import.FBX 0')
mesh = unreal.load_asset(NPC + '/Characters/SK_YiSunSin_NPC')
skeleton = mesh.get_editor_property('skeleton')
options = unreal.FbxImportUI()
for name, value in [('automated_import_should_detect_type', False), ('import_mesh', False),
                    ('import_as_skeletal', True), ('import_animations', True),
                    ('import_materials', False), ('import_textures', False)]:
    options.set_editor_property(name, value)
options.set_editor_property('mesh_type_to_import', unreal.FBXImportType.FBXIT_ANIMATION)
options.set_editor_property('skeleton', skeleton)
data = options.get_editor_property('anim_sequence_import_data')
data.set_editor_property('animation_length', unreal.FBXAnimationLengthImportType.FBXALIT_EXPORTED_TIME)
data.set_editor_property('remove_redundant_keys', False)
task = unreal.AssetImportTask()
task.filename = str(ART / 'FBX/AS_YiSunSin_NPC_Idle_Upright.fbx')
task.destination_path = NPC + '/Animations'
task.destination_name = 'AS_YiSunSin_NPC_Idle_Upright'
task.automated = True
task.replace_existing = True
task.save = False
task.factory = unreal.FbxFactory()
task.options = options
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
idle = next(obj for obj in task.get_objects() if isinstance(obj, unreal.AnimSequence))
idle.set_editor_property('enable_root_motion', False)
idle.set_editor_property('force_root_lock', False)
# UE strips the FBX object named Armature. For mesh import the bind matrices
# retain that object's transform, but animation import leaves root keys local
# to it. Restore the bind root exactly once, immediately after a fresh import.
component_for_bind = unreal.SkeletalMeshComponent()
component_for_bind.set_skeletal_mesh_asset(mesh)
bind_root = component_for_bind.get_ref_pose_transform(0)
assert str(component_for_bind.get_bone_name(0)) == 'spine'
root_keys = []
for frame in range(122):
    pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(
        idle, frame / 25.0, unreal.AnimPoseEvaluationOptions())
    local = unreal.AnimPoseExtensions.get_bone_pose(pose, 'spine', unreal.AnimPoseSpaces.LOCAL)
    assert max(abs(v-1.0) for v in [local.scale3d.x, local.scale3d.y, local.scale3d.z]) < 0.001
    root_keys.append(unreal.MathLibrary.compose_transforms(local, bind_root))
assert idle.controller.set_bone_track_keys('spine',
    [key.translation for key in root_keys], [key.rotation for key in root_keys],
    [key.scale3d for key in root_keys])
report['idle_root_bind_restored'] = True
report['idle_root_scale'] = [root_keys[0].scale3d.x, root_keys[0].scale3d.y, root_keys[0].scale3d.z]
unreal.EditorAssetLibrary.save_loaded_asset(idle, False)
report['idle'] = idle.get_path_name()
report['idle_duration_seconds'] = idle.get_play_length()

def own_copy(source, destination):
    asset = unreal.load_asset(destination)
    return asset or unreal.EditorAssetLibrary.duplicate_asset(source, destination)

blend_path = NPC + '/Animations/BS_YiSunSin_NPC_Ground'
blend = unreal.load_asset(blend_path)
if blend is None:
    factory = unreal.BlendSpaceFactory1D()
    factory.set_editor_property('target_skeleton', skeleton)
    factory.set_editor_property('preview_skeletal_mesh', mesh)
    blend = unreal.AssetToolsHelpers.get_asset_tools().create_asset('BS_YiSunSin_NPC_Ground', NPC+'/Animations', unreal.BlendSpace1D, factory)
if blend is None:
    raise RuntimeError('NPC ground blend-space factory failed')
parameters = list(blend.get_editor_property('blend_parameters'))
parameters[0].set_editor_property('display_name', 'Speed')
parameters[0].set_editor_property('min', 0.0)
parameters[0].set_editor_property('max', 450.0)
blend.set_editor_property('blend_parameters', parameters)
samples = []
for animation, speed in [(idle, 0.0), (unreal.load_asset(BASE+'/Animations/Latest/AS_YiSunSin_Walk'),150.0),
                         (unreal.load_asset(BASE+'/Animations/Latest/AS_YiSunSin_Run'),450.0)]:
    sample = unreal.BlendSample()
    sample.set_editor_property('animation', animation)
    sample.set_editor_property('sample_value', unreal.Vector(speed,0,0))
    samples.append(sample)
blend.set_editor_property('sample_data', samples)
unreal.SWFoleyMigrationLibrary.rebuild_contact_blend_space(blend)
unreal.EditorAssetLibrary.save_loaded_asset(blend, False)
report['blend_space'] = blend.get_path_name()
report['samples'] = [{'animation': s.get_editor_property('animation').get_path_name(),
                      'value': str(s.get_editor_property('sample_value'))} for s in samples]
abp = own_copy(BASE + '/Animations/ABP_YiSunSin', NPC + '/Animations/ABP_YiSunSin_NPC')
abp.set_editor_property('target_skeleton', skeleton)
nodes = []
graph = unreal.BlueprintEditorLibrary.find_graph(abp, 'AnimGraph')
objects = graph.get_graph_nodes_of_class(unreal.AnimGraphNode_BlendSpacePlayer)
for obj in objects:
    if obj.get_class().get_name() == 'AnimGraphNode_BlendSpacePlayer':
        node = obj.get_editor_property('node')
        node.set_editor_property('blend_space', blend)
        obj.set_editor_property('node', node)
        nodes.append(obj.get_path_name())
if len(nodes) != 1:
    raise RuntimeError('Unexpected blendspace graph structure: ' + str(nodes))
if not unreal.SWNpcAuthoringLibrary.configure_npc_locomotion_graph(abp, skeleton, blend):
    raise RuntimeError('NPC locomotion graph did not produce a connected, compiled pose')
unreal.EditorAssetLibrary.save_loaded_asset(abp, False)
report['animation_blueprint'] = abp.get_path_name()
report['modified_blendspace_nodes'] = nodes

bp_path = NPC + '/Blueprints/BP_YiSunSin_NPC'
bp = unreal.load_asset(bp_path)
if bp is None:
    factory = unreal.BlueprintFactory()
    factory.set_editor_property('parent_class', unreal.Character)
    bp = unreal.AssetToolsHelpers.get_asset_tools().create_asset('BP_YiSunSin_NPC', NPC + '/Blueprints', unreal.Blueprint, factory)
unreal.BlueprintEditorLibrary.compile_blueprint(bp)
actor = unreal.get_default_object(bp.generated_class())
actor.set_editor_property('auto_possess_player', unreal.AutoReceiveInput.DISABLED)
actor.set_editor_property('auto_possess_ai', unreal.AutoPossessAI.PLACED_IN_WORLD_OR_SPAWNED)
actor.set_editor_property('ai_controller_class', unreal.AIController)
actor.set_editor_property('use_controller_rotation_yaw', False)
capsule = actor.get_editor_property('capsule_component')
capsule.set_capsule_half_height(96.0)
capsule.set_capsule_radius(30.0)
component = actor.get_editor_property('mesh')
component.set_skeletal_mesh_asset(mesh)
component.set_anim_instance_class(abp.generated_class())
component.set_editor_property('relative_location', unreal.Vector(0,0,-96))
# Preserve the supplied character's orientation convention without inheriting its player BP.
original_class = unreal.EditorAssetLibrary.load_blueprint_class(BASE + '/Blueprints/BP_YiSunSin')
original = unreal.get_default_object(original_class).get_editor_property('mesh')
component.set_editor_property('relative_rotation', original.get_editor_property('relative_rotation'))
movement = actor.get_editor_property('character_movement')
movement.set_editor_property('orient_rotation_to_movement', True)
movement.set_editor_property('max_walk_speed', 150.0)
unreal.EditorAssetLibrary.save_loaded_asset(bp, False)
report['npc_blueprint'] = bp.get_path_name()
report['npc_parent_class'] = 'Character'
report['mesh_relative_rotation'] = str(component.get_editor_property('relative_rotation'))
report['auto_possess_player'] = str(actor.get_editor_property('auto_possess_player'))
report['auto_possess_ai'] = str(actor.get_editor_property('auto_possess_ai'))
report['references_original_walk_run'] = True
(ART / 'Audit/npc_setup.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
unreal.log('YISUNSIN_NPC_SETUP_COMPLETE')
