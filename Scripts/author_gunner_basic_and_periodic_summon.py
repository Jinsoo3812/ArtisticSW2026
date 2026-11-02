"""Keep basic shots and author the existing summon task on a 20-second BT loop."""
import shutil
from pathlib import Path
import unreal

ROOT = '/Game/GameplayAbilitySystem/Enemy/Balancing/T1/'


def backup(asset):
    relative = asset.get_path_name().split('.')[0].removeprefix('/Game/') + '.uasset'
    source = Path(unreal.Paths.project_content_dir()) / relative
    dest = Path(unreal.Paths.project_saved_dir()) / 'GunnerPeriodicSummonBackup' / relative
    if source.exists() and not dest.exists():
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, dest)


def save(asset):
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset):
        raise RuntimeError('Could not save ' + asset.get_path_name())


boss_bp = unreal.load_asset(ROOT + 'BP_T1Boss_Enemy')
tree = unreal.load_asset(ROOT + 'BT_T1Boss_GunnerCombat')
assert boss_bp and tree
backup(boss_bp)
backup(tree)
summon_path = ROOT + 'BP_T1Boss_SummonedRanged'
summon_bp = unreal.load_asset(summon_path)
if not summon_bp:
    summon_bp = unreal.EditorAssetLibrary.duplicate_asset(ROOT + 'T1_BP_DeckRangedEnemy', summon_path)
assert summon_bp
backup(summon_bp)
stats = unreal.load_asset('/Game/GameplayAbilitySystem/Enemy/Data/DT_EnemyBaseStat')
assert stats
summon_cdo = unreal.get_default_object(summon_bp.generated_class())
summon_cdo.set_editor_property('default_stats_row', unreal.DataTableRowHandle(
    data_table=stats, row_name='T1_BossSummon_Ranged'))
unreal.BlueprintEditorLibrary.compile_blueprint(summon_bp)
save(summon_bp)

unreal.BlueprintEditorLibrary.compile_blueprint(boss_bp)
boss = unreal.get_default_object(boss_bp.generated_class())
burst = unreal.load_class(None, '/Script/Enemy.GA_BossGunBurst')
basic = unreal.load_class(None, '/Script/Enemy.GA_BossGunAttack')
abilities = [ability for ability in boss.get_editor_property('starting_abilities') if ability != burst]
if basic not in abilities:
    abilities.append(basic)
boss.set_editor_property('starting_abilities', abilities)
boss.set_editor_property('summoned_enemy_class', summon_bp.generated_class())
boss.set_editor_property('additional_summon_pool_capacity', 2)
boss.set_editor_property('max_summoned_deck_enemies', 2)
boss.set_editor_property('summon_cooldown', 0.0)
# Periodic summoning is scheduled by BT rather than the old HP threshold waves.
boss.set_editor_property('encounter_balance_row', unreal.DataTableRowHandle())
assert unreal.GunnerCombatEditorLibrary.configure_gunner_summon_tree(tree, 20.0)
save(tree)
save(boss_bp)
unreal.log('GUNNER_BASIC_AND_PERIODIC_SUMMON_APPLIED')
