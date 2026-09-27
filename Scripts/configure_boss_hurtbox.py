"""Repair the authored boss hit surface without changing its mesh or animations."""
import unreal

boss = unreal.load_asset('/Game/GameplayAbilitySystem/Enemy/BP_Ship_BossEnemy')
cdo = unreal.get_default_object(boss.generated_class())
mesh_component = cdo.get_editor_property('mesh')
mesh = mesh_component.get_editor_property('skeletal_mesh_asset')
physics = unreal.load_asset('/Game/Fab/Samurai/SKM_Samurai_PhysicsAsset')
if not mesh or not physics:
    raise RuntimeError('Boss mesh or Samurai Physics Asset is missing')
unreal.log('BossHurtbox mesh=' + mesh.get_path_name())
unreal.log('BossHurtbox old physics=' + str(mesh.get_editor_property('physics_asset')))
mesh.set_editor_property('physics_asset', physics)
mesh_component.set_editor_property('physics_asset_override', physics)
hurtbox = cdo.get_editor_property('combat_hurtbox_component')
hurtbox.set_editor_property('mode', unreal.CombatHurtboxMode.ANIMATED_PHYSICS_ASSET)
hurtbox.set_editor_property('profile_name', 'CharacterHurtbox')
unreal.BlueprintEditorLibrary.compile_blueprint(boss)
if not unreal.EditorAssetLibrary.save_loaded_asset(mesh, only_if_is_dirty=False):
    raise RuntimeError('Failed to save boss skeletal mesh')
if not unreal.EditorAssetLibrary.save_loaded_asset(boss, only_if_is_dirty=False):
    raise RuntimeError('Failed to save boss Blueprint')
unreal.log('BossHurtbox authoring saved')
