import unreal


MONTAGE_DIR = "/Game/Fab/Samurai/Montages"
ATTACK_SET_PATH = "/Game/GameplayAbilitySystem/Enemy/DA/DA_RogueBossBasicAttacks"
BOSS_BP_PATH = "/Game/GameplayAbilitySystem/Enemy/BP_Ship_BossEnemy"


def create_montage(asset_name, sequence_path):
    asset_path = f"{MONTAGE_DIR}/{asset_name}"
    sequence = unreal.EditorAssetLibrary.load_asset(sequence_path)
    if not sequence:
        raise RuntimeError(f"Missing source animation: {sequence_path}")

    montage = unreal.EditorAssetLibrary.load_asset(asset_path)
    if not montage:
        factory = unreal.AnimMontageFactory()
        factory.set_editor_property("source_animation", sequence)
        montage = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            asset_name, MONTAGE_DIR, unreal.AnimMontage, factory)
        if not montage:
            raise RuntimeError(f"Failed to create montage: {asset_path}")

    unreal.EditorAssetLibrary.save_loaded_asset(montage)
    return montage


def create_attack_set(short_a, short_b, combo):
    attack_set = unreal.EditorAssetLibrary.load_asset(ATTACK_SET_PATH)
    if not attack_set:
        factory = unreal.DataAssetFactory()
        factory.set_editor_property("data_asset_class", unreal.BossBasicAttackSet)
        attack_set = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            "DA_RogueBossBasicAttacks",
            "/Game/GameplayAbilitySystem/Enemy/DA",
            unreal.BossBasicAttackSet,
            factory)
    if not attack_set:
        raise RuntimeError("Failed to create BossBasicAttackSet")

    short_a_entry = unreal.BossBasicAttackEntry(
        attack_id="ShortA",
        attack_type=unreal.BossBasicAttackType.SHORT,
        attack_montage=short_a,
        attack_montage_play_rate=1.0,
        selection_weight=1.0,
        use_timed_hit_scan_window=True,
        timed_hit_scan_start_normalized=0.35,
        timed_hit_scan_duration_normalized=0.25)
    short_b_entry = unreal.BossBasicAttackEntry(
        attack_id="ShortB",
        attack_type=unreal.BossBasicAttackType.SHORT,
        attack_montage=short_b,
        attack_montage_play_rate=1.0,
        selection_weight=1.0,
        use_timed_hit_scan_window=True,
        timed_hit_scan_start_normalized=0.35,
        timed_hit_scan_duration_normalized=0.25)
    combo_cooldown_tag = unreal.GameplayTag()
    if not combo_cooldown_tag.import_text("Cooldown.Boss.BasicAttack.Combo"):
        raise RuntimeError("Failed to resolve Combo cooldown GameplayTag")
    combo_entry = unreal.BossBasicAttackEntry(
        attack_id="Combo",
        attack_type=unreal.BossBasicAttackType.COMBO,
        attack_montage=combo,
        attack_montage_play_rate=1.0,
        selection_weight=0.6,
        individual_cooldown_tag=combo_cooldown_tag,
        individual_cooldown_duration=6.0)

    attack_set.set_editor_property("attacks", [short_a_entry, short_b_entry, combo_entry])
    attack_set.set_editor_property("avoid_immediate_repeat", True)
    unreal.EditorAssetLibrary.save_loaded_asset(attack_set)
    return attack_set


def assign_to_boss(attack_set):
    blueprint = unreal.EditorAssetLibrary.load_asset(BOSS_BP_PATH)
    if not blueprint:
        raise RuntimeError(f"Missing Boss Blueprint: {BOSS_BP_PATH}")
    generated_class = blueprint.generated_class()
    boss_cdo = unreal.get_default_object(generated_class)
    boss_cdo.set_editor_property("basic_attack_set", attack_set)
    boss_basic_attack_class = unreal.load_class(None, "/Script/Enemy.GA_BossBasicAttack")
    starting_abilities = list(boss_cdo.get_editor_property("starting_abilities"))
    if boss_basic_attack_class not in starting_abilities:
        starting_abilities.append(boss_basic_attack_class)
        boss_cdo.set_editor_property("starting_abilities", starting_abilities)
    unreal.EditorAssetLibrary.save_loaded_asset(blueprint)


def main():
    short_a = create_montage(
        "AM_Samurai_Attack1",
        "/Game/Fab/Samurai/Anim_Samurai_Attack1")
    short_b = create_montage(
        "AM_Samurai_Attack2",
        "/Game/Fab/Samurai/Anim_Samurai_Attack2")
    combo = unreal.EditorAssetLibrary.load_asset(
        "/Game/Sword_Anims/Animations/HandsomeSwordV2/Manny_UE5/RootMotion/Attack/ComboAttack/AM_SwordCombo")
    if not combo:
        raise RuntimeError("Missing existing AM_SwordCombo")

    attack_set = create_attack_set(short_a, short_b, combo)
    assign_to_boss(attack_set)
    unreal.log("Boss basic attack assets created and assigned successfully.")


main()
