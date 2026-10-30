"""Reload NPC assets, validate their saved references, and build a small review map."""
import json
from pathlib import Path
import unreal

ROOT = Path(unreal.Paths.project_dir()).resolve()
ART = ROOT / 'SourceArt/Characters/YiSunSin'
BASE = '/Game/Chae_Asset/YiSunSinContent'
NPC = BASE + '/NPC'
mesh = unreal.load_asset(NPC+'/Characters/SK_YiSunSin_NPC')
idle = unreal.load_asset(NPC+'/Animations/AS_YiSunSin_NPC_Idle_Upright')
blend = unreal.load_asset(NPC+'/Animations/BS_YiSunSin_NPC_Ground')
abp = unreal.load_asset(NPC+'/Animations/ABP_YiSunSin_NPC')
bp = unreal.load_asset(NPC+'/Blueprints/BP_YiSunSin_NPC')
skeleton = mesh.get_editor_property('skeleton')
bind_component = unreal.SkeletalMeshComponent()
bind_component.set_skeletal_mesh_asset(mesh)
bind_root = bind_component.get_ref_pose_transform(0)
for time in [0.0, 2.42, 4.84]:
    pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(idle,time,unreal.AnimPoseEvaluationOptions())
    root = unreal.AnimPoseExtensions.get_bone_pose(pose,'spine',unreal.AnimPoseSpaces.LOCAL)
    assert max(abs(a-b) for a,b in zip(
        [root.scale3d.x,root.scale3d.y,root.scale3d.z],
        [bind_root.scale3d.x,bind_root.scale3d.y,bind_root.scale3d.z])) < 0.01
    dot = abs(root.rotation.x*bind_root.rotation.x + root.rotation.y*bind_root.rotation.y +
              root.rotation.z*bind_root.rotation.z + root.rotation.w*bind_root.rotation.w)
    assert dot > 0.99999, 'Idle root lost the original bind rotation'
assert idle.get_editor_property('skeleton') == skeleton
assert blend.get_editor_property('skeleton') == skeleton
assert abp.get_editor_property('target_skeleton') == skeleton
assert isinstance(unreal.get_default_object(abp.generated_class()), unreal.SWNpcAnimInstance)
graph = unreal.BlueprintEditorLibrary.find_graph(abp, 'AnimGraph')
players = graph.get_graph_nodes_of_class(unreal.AnimGraphNode_BlendSpacePlayer)
assert len(players) == 1
assert players[0].get_editor_property('node').get_editor_property('blend_space') == blend
unreal.BlueprintEditorLibrary.compile_blueprint(abp)
unreal.BlueprintEditorLibrary.compile_blueprint(bp)
cdo = unreal.get_default_object(bp.generated_class())
assert isinstance(cdo, unreal.Character)
assert cdo.get_editor_property('auto_possess_player') == unreal.AutoReceiveInput.DISABLED
assert cdo.get_editor_property('mesh').get_editor_property('skeletal_mesh') == mesh
assert cdo.get_editor_property('mesh').get_editor_property('anim_class') == abp.generated_class()
samples = blend.get_editor_property('sample_data')
assert [s.get_editor_property('sample_value').x for s in samples] == [0.0,150.0,450.0]
assert samples[0].get_editor_property('animation') == idle
assert abs(idle.get_play_length()-4.84) < 0.001
report = {'saved_references_valid': True, 'compiled_animation_blueprint': abp.get_path_name(),
          'compiled_npc_blueprint': bp.get_path_name(), 'original_skeleton_shared': skeleton.get_path_name(),
          'player_input_disabled': True, 'parent': 'Character', 'idle_duration_seconds': idle.get_play_length(),
          'speed_samples_cm_per_sec': [0,150,450], 'runtime_pie_report': 'ue_blend_samples.json',
          'idle_root_scale_and_rotation_match_bind': True}
(ART/'Audit/saved_assets_validation.json').write_text(json.dumps(report,indent=2),encoding='utf-8')

level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
map_path = NPC+'/Preview/L_YiSunSin_NPC_Preview'
def ensure_preview():
    if unreal.EditorAssetLibrary.does_asset_exist(map_path):
        assert level.load_level(map_path)
        report['preview_map'] = map_path
        report['existing_preview_preserved'] = True
        return
    if not level.new_level(map_path):
        raise RuntimeError('Could not create isolated NPC preview map')
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    floor = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0,0,-5))
    floor.set_actor_label('Preview floor')
    floor.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
    floor.set_actor_scale3d(unreal.Vector(8,8,0.1))
    sun = actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0,0,400), unreal.Rotator(-35,-35,0))
    sun.light_component.set_editor_property('intensity',3.0)
    actors.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0,0,300))
    npc = actors.spawn_actor_from_class(bp.generated_class(), unreal.Vector(0,100,96))
    npc.set_actor_label('YiSunSin NPC upright idle')
    npc.get_editor_property('mesh').set_update_animation_in_editor(True)
    before = actors.spawn_actor_from_class(unreal.SkeletalMeshActor, unreal.Vector(0,-150,0), unreal.Rotator(0,-90,0))
    before.set_actor_label('Original mesh and original idle reference')
    before.skeletal_mesh_component.set_skeletal_mesh_asset(unreal.load_asset(BASE+'/Characters/SK_YiSunSin_Latest'))
    before.skeletal_mesh_component.play_animation(unreal.load_asset(BASE+'/Animations/Latest/AS_YiSunSin_Idle'),True)
    before.skeletal_mesh_component.set_update_animation_in_editor(True)
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    world.get_world_settings().set_editor_property('default_game_mode', unreal.GameModeBase)
    level.save_current_level()
    report['preview_map'] = map_path
    report['registered_anim_instance'] = npc.get_editor_property('mesh').get_anim_instance().get_class().get_name()
    (ART/'Audit/saved_assets_validation.json').write_text(json.dumps(report,indent=2),encoding='utf-8')

ensure_preview()
(ART/'Audit/saved_assets_validation.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
unreal.log('YISUNSIN_NPC_SAVED_ASSETS_VERIFIED')
