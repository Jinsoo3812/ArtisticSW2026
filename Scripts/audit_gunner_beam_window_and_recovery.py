import math
import unreal

root = '/Game/GameplayAbilitySystem/Enemy/Balancing/T1/'
bp = unreal.load_asset(root + 'BP_T1Boss_Enemy')
boss = unreal.get_default_object(bp.generated_class())
combat = boss.get_editor_property('combat_data')
ray = combat.get_editor_property('ray')
assert math.isclose(ray.get_editor_property('pre_shot_show_time'), 0.5, abs_tol=0.001)
anim_bp = unreal.load_asset(root + 'Animations/ABP_T1Boss_Rifle')
for name, expected in [('basic', 0.35), ('burst', 0.5)]:
    pattern = combat.get_editor_property(name)
    assert math.isclose(pattern.get_editor_property('recovery_time'), expected, abs_tol=0.001)
    assert unreal.GunnerAnimationEditorLibrary.audit_combat_assets(anim_bp, pattern.get_editor_property('montage'))
    unreal.log('GUNNER_TIMING ' + name + ' recovery=' + str(pattern.get_editor_property('recovery_time')))
unreal.log('GUNNER_BEAM_TIMING_RELOAD_OK pre=0.5 end=attack_completion muzzle_scale=' + str(combat.get_editor_property('muzzle_flash_scale')))
