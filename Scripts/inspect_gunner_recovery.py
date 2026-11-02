import unreal

bp = unreal.load_asset('/Game/GameplayAbilitySystem/Enemy/Balancing/T1/BP_T1Boss_Enemy')
combat = unreal.get_default_object(bp.generated_class()).get_editor_property('combat_data')
for name in ['basic', 'burst']:
    pattern = combat.get_editor_property(name)
    for key in ['prepare_time', 'lock_time', 'recovery_time', 'shot_gap', 'post_attack_gap', 'montage_play_rate', 'montage']:
        unreal.log('GUNNER_RECOVERY ' + name + '.' + key + '=' + str(pattern.get_editor_property(key)))
    montage = pattern.get_editor_property('montage')
