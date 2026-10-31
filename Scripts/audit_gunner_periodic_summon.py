"""Run in a fresh editor process to verify saved combat and summon bindings."""
import unreal
import runpy
from pathlib import Path

ROOT = '/Game/GameplayAbilitySystem/Enemy/Balancing/T1/'
boss_bp = unreal.load_asset(ROOT + 'BP_T1Boss_Enemy')
boss = unreal.get_default_object(boss_bp.generated_class())
abilities = boss.get_editor_property('starting_abilities')
assert unreal.load_class(None, '/Script/Enemy.GA_BossGunBurst') not in abilities
assert sum(ability == unreal.load_class(None, '/Script/Enemy.GA_BossGunAttack') for ability in abilities) == 1
assert boss.get_editor_property('additional_summon_pool_capacity') == 2
assert boss.get_editor_property('max_summoned_deck_enemies') == 2
assert boss.get_editor_property('summon_cooldown') == 0
assert not boss.get_editor_property('encounter_balance_row').get_editor_property('data_table')
summon_bp = unreal.load_asset(ROOT + 'BP_T1Boss_SummonedRanged')
assert boss.get_editor_property('summoned_enemy_class') == summon_bp.generated_class()
summon = unreal.get_default_object(summon_bp.generated_class())
assert summon.get_editor_property('deck_combat_role') == unreal.DeckEnemyCombatRole.RANGED
row = summon.get_editor_property('default_stats_row')
assert str(row.get_editor_property('row_name')) == 'T1_BossSummon_Ranged'
assert row.get_editor_property('data_table') == unreal.load_asset('/Game/GameplayAbilitySystem/Enemy/Data/DT_EnemyBaseStat')
assert unreal.GunnerCombatEditorLibrary.audit_gunner_summon_tree(
    unreal.load_asset(ROOT + 'BT_T1Boss_GunnerCombat'), 20.0)
unreal.log('GUNNER_PERIODIC_SUMMON_SAVED_AUDIT_OK')
for script in ['inspect_t1_gunner.py', 'audit_gunner_beam_window_and_recovery.py',
               'audit_gunner_shot_and_deck_focus.py']:
    runpy.run_path(str(Path(__file__).with_name(script)))
