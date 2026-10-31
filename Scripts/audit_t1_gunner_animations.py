"""Reload audit; no saving or gameplay changes."""
import unreal

FOLDER = '/Game/GameplayAbilitySystem/Enemy/Balancing/T1/'
ANIMS = FOLDER + 'Animations/'
bp = unreal.load_asset(FOLDER + 'BP_T1Boss_Enemy')
boss = unreal.get_default_object(bp.generated_class())
anim_bp = unreal.load_asset(ANIMS + 'ABP_T1Boss_Rifle')
assert boss.get_editor_property('mesh').get_editor_property('anim_class') == anim_bp.generated_class()
combat = boss.get_editor_property('combat_data')
for name, suffix in [('basic', 'Shot'), ('burst', 'Burst')]:
    pattern = combat.get_editor_property(name)
    montage = unreal.load_asset(ANIMS + 'AM_T1Boss_Rifle' + suffix)
    assert pattern.get_editor_property('montage') == montage
    assert pattern.get_editor_property('prepare_section') == 'Aim'
    assert pattern.get_editor_property('fire_section') == 'Fire'
    assert pattern.get_editor_property('recovery_section') == 'Recover'
    assert pattern.get_editor_property('lock_time') > 0
    assert unreal.GunnerAnimationEditorLibrary.audit_combat_assets(anim_bp, montage)
fire = unreal.load_asset(ANIMS + 'AS_T1Boss_RifleFire')
assert fire.get_editor_property('additive_anim_type') == unreal.AdditiveAnimationType.AAT_NONE
original = unreal.load_asset('/Game/Characters/Mannequins/Anims/Rifle/MM_Rifle_Fire')
assert original.get_editor_property('additive_anim_type') == unreal.AdditiveAnimationType.AAT_ROTATION_OFFSET_MESH_SPACE
ray = combat.get_editor_property('ray')
assert ray.get_editor_property('color') == ray.get_editor_property('locked_color')
assert ray.get_editor_property('color') != ray.get_editor_property('shot_color')
unreal.log('T1_GUNNER_ANIMATION_RELOAD_AUDIT_OK')
