import unreal


BOSS_CUE_FOLDER = "/Game/GameplayCues/Boss"
WEAPON_CUE_FOLDER = "/Game/GameplayCues/Impact/Weapon"
ABILITY_IMPACT_CUE_FOLDER = "/Game/GameplayCues/Impact/Boss"
WEAPON_DATA_ASSET = "/Game/GameplayAbilitySystem/Enemy/Weapon/DA_Weapon"
FEEDBACK_PARENT = "/Script/ArtisticSWCore.SWGameplayCueNotify_BurstFeedback"
CAMERA_SHAKE_CLASS = (
    "/Game/Variant_Combat/Blueprints/"
    "BP_CameraShake_Hit_Enemy.BP_CameraShake_Hit_Enemy_C"
)
HIT_NIAGARA = "/Game/Variant_Combat/VFX/NS_Damage.NS_Damage"


def load_required(path, loader, label):
    value = loader(path)
    if value is None:
        raise RuntimeError(f"Could not load {label}: {path}")
    return value


def set_gameplay_cue_tag(cdo, tag_name):
    tag = unreal.GameplayTag()
    if not tag.import_text(tag_name):
        raise RuntimeError(f"Could not import gameplay tag: {tag_name}")
    cdo.set_editor_property("gameplay_cue_tag", tag)


def create_or_update_cue(folder, asset_name, tag_name, configure):
    asset_path = f"{folder}/{asset_name}"
    blueprint = None
    if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        blueprint = unreal.EditorAssetLibrary.load_asset(asset_path)
    if blueprint is None:
        factory = unreal.BlueprintFactory()
        parent_class = load_required(
            FEEDBACK_PARENT,
            lambda path: unreal.load_class(None, path),
            "feedback cue parent class",
        )
        factory.set_editor_property("parent_class", parent_class)
        blueprint = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            asset_name,
            folder,
            unreal.Blueprint,
            factory,
        )
    if blueprint is None:
        raise RuntimeError(f"Could not create {asset_path}")

    generated_class = blueprint.generated_class()
    cdo = unreal.get_default_object(generated_class)
    set_gameplay_cue_tag(cdo, tag_name)
    configure(cdo)
    if not unreal.EditorAssetLibrary.save_loaded_asset(blueprint, only_if_is_dirty=False):
        raise RuntimeError(f"Could not save {asset_path}")
    unreal.log(f"Authored boss gameplay cue: {asset_path}")


camera_shake_class = load_required(
    CAMERA_SHAKE_CLASS,
    lambda path: unreal.load_class(None, path),
    "camera shake class",
)
hit_niagara = load_required(HIT_NIAGARA, unreal.EditorAssetLibrary.load_asset, "hit Niagara")


def configure_attack(cdo):
    cdo.set_editor_property("camera_shake_class", camera_shake_class)
    cdo.set_editor_property("camera_shake_scale", 0.25)
    cdo.set_editor_property(
        "camera_shake_recipient",
        unreal.SWGameplayCueCameraShakeRecipient.ALL_LOCAL_PLAYERS_IN_RADIUS,
    )
    cdo.set_editor_property("camera_shake_inner_radius", 300.0)
    cdo.set_editor_property("camera_shake_outer_radius", 2200.0)
    cdo.set_editor_property("camera_shake_falloff", 1.5)
    cdo.set_editor_property("niagara_system", None)


def configure_hit(cdo):
    cdo.set_editor_property("camera_shake_class", camera_shake_class)
    cdo.set_editor_property("camera_shake_scale", 1.0)
    cdo.set_editor_property(
        "camera_shake_recipient",
        unreal.SWGameplayCueCameraShakeRecipient.INSTIGATOR_LOCAL_PLAYER,
    )
    cdo.set_editor_property("niagara_system", hit_niagara)


def configure_confirmed_target_impact(cdo):
    cdo.set_editor_property("camera_shake_class", camera_shake_class)
    cdo.set_editor_property("camera_shake_scale", 1.0)
    cdo.set_editor_property(
        "camera_shake_recipient",
        unreal.SWGameplayCueCameraShakeRecipient.TARGET_LOCAL_PLAYER,
    )
    cdo.set_editor_property("niagara_system", hit_niagara)


def make_gameplay_tag(tag_name):
    tag = unreal.GameplayTag()
    if not tag.import_text(tag_name):
        raise RuntimeError(f"Could not import gameplay tag: {tag_name}")
    return tag


def assign_weapon_impact_cues():
    weapon_data = load_required(
        WEAPON_DATA_ASSET,
        unreal.EditorAssetLibrary.load_asset,
        "enemy weapon data asset",
    )
    tag_by_weapon = {
        "Item.EnemyWeapon.Hand": "GameplayCue.Impact.Weapon.Hand",
        "Item.EnemyWeapon.Sword": "GameplayCue.Impact.Weapon.Sword",
        "Item.EnemyWeapon.Bow": "GameplayCue.Impact.Weapon.Bow",
    }
    definitions = weapon_data.get_editor_property("weapon_definitions")
    updated = []
    for definition in definitions:
        weapon_gameplay_tag = definition.get_editor_property("weapon_tag")
        weapon_tag = str(weapon_gameplay_tag.get_editor_property("tag_name"))
        impact_tag_name = tag_by_weapon.get(weapon_tag)

        source_combat_data = definition.get_editor_property("combat_data")
        impact_tag = make_gameplay_tag(impact_tag_name) if impact_tag_name else unreal.GameplayTag()
        combat_data = unreal.WeaponCombatData(
            damage_effect_class=source_combat_data.get_editor_property("damage_effect_class"),
            impact_gameplay_cue_tag=impact_tag,
            attack_montage=source_combat_data.get_editor_property("attack_montage"),
            attack_montage_play_rate=source_combat_data.get_editor_property("attack_montage_play_rate"),
            attack_range=source_combat_data.get_editor_property("attack_range"),
        )
        if impact_tag_name:
            unreal.log(f"Assigned {impact_tag_name} to {weapon_tag}")

        updated_definition = unreal.WeaponDefinition(
            weapon_tag=definition.get_editor_property("weapon_tag"),
            weapon_type_tags=definition.get_editor_property("weapon_type_tags"),
            weapon_actor_class=definition.get_editor_property("weapon_actor_class"),
            socket_data=definition.get_editor_property("socket_data"),
            ability_data=definition.get_editor_property("ability_data"),
            combat_data=combat_data,
        )
        updated.append(updated_definition)
    weapon_data.set_editor_property("weapon_definitions", updated)
    if not unreal.EditorAssetLibrary.save_loaded_asset(weapon_data, only_if_is_dirty=False):
        raise RuntimeError(f"Could not save {WEAPON_DATA_ASSET}")


create_or_update_cue(
    BOSS_CUE_FOLDER,
    "GCN_Boss_Attack",
    "GameplayCue.Boss.Attack",
    configure_attack,
)
create_or_update_cue(
    BOSS_CUE_FOLDER,
    "GCN_Boss_Hit",
    "GameplayCue.Boss.Hit",
    configure_hit,
)

for weapon_name in ("Hand", "Sword", "Bow"):
    create_or_update_cue(
        WEAPON_CUE_FOLDER,
        f"GCN_Impact_Weapon_{weapon_name}",
        f"GameplayCue.Impact.Weapon.{weapon_name}",
        configure_confirmed_target_impact,
    )

for ability_name in ("DashSlash", "Knockback"):
    create_or_update_cue(
        ABILITY_IMPACT_CUE_FOLDER,
        f"GCN_Impact_Boss_{ability_name}",
        f"GameplayCue.Impact.Boss.{ability_name}",
        configure_confirmed_target_impact,
    )

assign_weapon_impact_cues()
