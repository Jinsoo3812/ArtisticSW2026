"""Create/update the minimal Enemy Bow asset and register its GA loadout."""

import unreal


BOW_PACKAGE = "/Game/GameplayAbilitySystem/Enemy/Weapon"
BOW_ASSET_NAME = "BP_EnemyBow"
BOW_MESH_PATH = (
    "/Game/GameplayAbilitySystem/Weapon/Bow/"
    "pbr-low-poly-game-ready-asset-bow-and-arrow/source/SM_Bow"
)
WEAPON_REGISTRY_PATH = "/Game/GameplayAbilitySystem/Enemy/Weapon/DA_Weapon"


def create_blueprint(asset_name, package_path, parent_class):
    asset_path = f"{package_path}/{asset_name}"
    existing = unreal.load_asset(asset_path)
    if existing:
        return existing

    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", parent_class)
    result = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        asset_name, package_path, unreal.Blueprint, factory
    )
    if not result:
        raise RuntimeError(f"Failed to create Blueprint: {asset_path}")
    return result


bow_blueprint = create_blueprint(
    BOW_ASSET_NAME,
    BOW_PACKAGE,
    unreal.EnemyBow.static_class(),
)
unreal.BlueprintEditorLibrary.compile_blueprint(bow_blueprint)
bow_class = bow_blueprint.generated_class()
bow_cdo = unreal.get_default_object(bow_class)
bow_mesh_asset = unreal.load_asset(BOW_MESH_PATH)
if not bow_mesh_asset:
    raise RuntimeError(f"Bow mesh is missing: {BOW_MESH_PATH}")

weapon_mesh = None
for component in bow_cdo.get_components_by_class(unreal.StaticMeshComponent):
    if component.get_name() == "WeaponMesh":
        weapon_mesh = component
        break
if not weapon_mesh:
    raise RuntimeError("BP_EnemyBow inherited WeaponMesh was not found")
weapon_mesh.set_editor_property("static_mesh", bow_mesh_asset)

weapon_registry = unreal.load_asset(WEAPON_REGISTRY_PATH)
if not weapon_registry:
    raise RuntimeError(f"Weapon registry is missing: {WEAPON_REGISTRY_PATH}")

bow_tag = unreal.EnemyBow.get_enemy_bow_weapon_tag()


def tag_text(tag):
    return str(tag.get_editor_property("tag_name"))


definitions = list(weapon_registry.get_editor_property("weapon_definitions"))
existing_bow = next(
    (
        definition
        for definition in definitions
        if tag_text(definition.get_editor_property("weapon_tag")) == "Item.EnemyWeapon.Bow"
    ),
    None,
)
if not existing_bow:
    granted_ability = unreal.GrantedWeaponAbility(
        ability_class=unreal.GA_RangedEnemyAttack.static_class(),
        ability_level=1,
    )
    ability_data = unreal.WeaponAbilityData(granted_abilities=[granted_ability])
    socket_data = unreal.WeaponSocketData(
        back_socket_name=unreal.Name("BackSocket"),
        equip_socket_name=unreal.Name("EquipSocket"),
    )
    combat_data = unreal.WeaponCombatData(
        attack_range=2000.0,
        attack_montage_play_rate=1.0,
    )
    definitions.append(
        unreal.WeaponDefinition(
            weapon_tag=bow_tag,
            weapon_actor_class=bow_class,
            socket_data=socket_data,
            ability_data=ability_data,
            combat_data=combat_data,
        )
    )
    weapon_registry.set_editor_property("weapon_definitions", definitions)

unreal.EditorAssetLibrary.save_loaded_asset(bow_blueprint, only_if_is_dirty=False)
unreal.EditorAssetLibrary.save_loaded_asset(weapon_registry, only_if_is_dirty=False)

saved_definitions = weapon_registry.get_editor_property("weapon_definitions")
saved_bow = next(
    (
        definition
        for definition in saved_definitions
        if tag_text(definition.get_editor_property("weapon_tag")) == "Item.EnemyWeapon.Bow"
    ),
    None,
)
if not saved_bow:
    raise RuntimeError("Enemy Bow definition was not saved")
if saved_bow.get_editor_property("weapon_actor_class") != bow_class:
    raise RuntimeError("Enemy Bow definition does not reference BP_EnemyBow")

unreal.log_warning(
    "[RangedEnemyBowAssets] BP_EnemyBow created and Item.EnemyWeapon.Bow registered."
)
