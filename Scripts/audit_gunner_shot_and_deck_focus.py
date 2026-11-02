import unreal

ROOT = '/Game/GameplayAbilitySystem/Enemy/Balancing/T1/'
bp = unreal.load_asset(ROOT + 'BP_T1Boss_Enemy')
combat = unreal.get_default_object(bp.generated_class()).get_editor_property('combat_data')
ray = combat.get_editor_property('ray')
material = unreal.load_asset(ROOT + 'VFX/M_T1Boss_ShotCylinder')
assert ray.get_editor_property('shot_material') == material
assert ray.get_editor_property('shot_radius') > 0
assert ray.get_editor_property('shot_brightness') > 0
assert material.get_editor_property('blend_mode') == unreal.BlendMode.BLEND_ADDITIVE
assert material.get_editor_property('shading_model') == unreal.MaterialShadingModel.MSM_UNLIT
flash = unreal.load_asset(ROOT + 'VFX/NS_T1Boss_MuzzleFlash')
assert isinstance(flash, unreal.NiagaraSystem)
assert combat.get_editor_property('muzzle_flash') == flash
assert combat.get_editor_property('muzzle_flash_scale') > 0
trees = [ROOT + 'BT_T1Boss_GunnerCombat']
for path in unreal.EditorAssetLibrary.list_assets('/Game/GameplayAbilitySystem/Enemy/AI/SubTree', recursive=True):
    if 'Combat' in path and any(name in path for name in ['/DeckMelee/', '/DeckRanged/', '/RogueBoss/', '/SamuraiBoss/', '/T1Shipboss/', '/T3ShipBoss/']):
        trees.append(path)
for path in trees:
    tree = unreal.load_asset(path)
    if isinstance(tree, unreal.BehaviorTree):
        assert unreal.GunnerCombatEditorLibrary.audit_task_focus(tree), path
unreal.log('GUNNER_SHOT_FOCUS_RELOAD_AUDIT_OK trees=' + str(len(trees)))
