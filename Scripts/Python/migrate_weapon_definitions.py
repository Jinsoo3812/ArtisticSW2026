"""Run once with UnrealEditor-Cmd -run=pythonscript -script=<this file>.

Migrates ONLY the four previously authored playable weapon rows. Does not save
player/enemy/map packages or infer balance for unauthored weapon tiers.
Re-running validates existing definitions instead of overwriting designer edits.
"""
import unreal

ROOT = '/Game/Blueprints/DAs/Weapon_DAs/Definitions'
REGISTRY = '/Game/Blueprints/Item/DA_ItemData'
PROFILES = {
    'SwordA1': ('/Game/GameplayAbilitySystem/Weapon/BP_BaseSwordA', '/Game/Blueprints/DAs/Weapon_DAs/Sword/WDA_SwordA'),
    'SwordA2': ('/Game/GameplayAbilitySystem/Weapon/BP_BaseSwordB', '/Game/Blueprints/DAs/Weapon_DAs/Sword/WDA_SwordB'),
    'ShortBow1': ('/Game/GameplayAbilitySystem/Weapon/BP_Bow', '/Game/Blueprints/DAs/Weapon_DAs/Bow/WDA_Bow'),
    'LongBow1': ('/Game/GameplayAbilitySystem/Weapon/BP_Bow', '/Game/Blueprints/DAs/Weapon_DAs/Bow/WDA_Bow'),
}


def require(value, message):
    if not value:
        raise RuntimeError(message)
    return value


def asset(name, cls):
    path = ROOT + '/' + name
    existing = unreal.EditorAssetLibrary.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
    if existing:
        return existing, False
    factory = unreal.DataAssetFactory()
    factory.set_editor_property('data_asset_class', cls)
    result = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, ROOT, cls, factory)
    return require(result, 'Failed creating ' + path), True


registry = require(unreal.load_asset(REGISTRY), REGISTRY)
rows = dict(registry.get_editor_property('item_definitions'))
saved = []
for tag, row in rows.items():
    tag_name = str(tag.get_editor_property('tag_name'))
    leaf = tag_name.rsplit('.', 1)[-1]
    if leaf not in PROFILES:
        continue
    actor_path, animation_path = PROFILES[leaf]
    actor_cls = require(unreal.EditorAssetLibrary.load_blueprint_class(actor_path), actor_path)
    actor = unreal.get_default_object(actor_cls)
    animation = require(unreal.load_asset(animation_path), animation_path)
    definition, new = asset('WD_' + leaf, unreal.EquippableWeaponDefinition)
    if new:
        ability = require(row.get_editor_property('granted_ability_class'), 'Missing GA for ' + tag_name)
        input_tag = row.get_editor_property('use_key_tag')
        abilities, _ = asset('WAS_' + leaf, unreal.WeaponAbilitySet)
        entry = unreal.WeaponAbilityEntry(input_tag=input_tag, ability_class=ability, level=1)
        abilities.set_editor_property('abilities', [entry])
        combat, _ = asset('WCD_' + leaf, unreal.WeaponCombatDataAsset)
        combat.set_editor_property('strength_bonus', actor.get_editor_property('strength_bonus'))
        projectile = row.get_editor_property('spawn_class')
        if projectile:
            combat.set_editor_property('projectile_class', projectile)
            coefficient_owner = unreal.get_default_object(projectile)
        else:
            coefficient_owner = actor
        combat.set_editor_property('attack_coefficient', coefficient_owner.get_attack_coefficient())
        roles = unreal.GameplayTagContainer(gameplay_tags=row.get_editor_property('can_use_class_list'))
        definition.set_editor_property('actor_class', actor_cls)
        definition.set_editor_property('ability_set', abilities)
        definition.set_editor_property('animation_data', animation)
        definition.set_editor_property('combat_data', combat)
        definition.set_editor_property('allowed_role_tags', roles)
        for obj in (abilities, combat, definition):
            require(unreal.EditorAssetLibrary.save_loaded_asset(obj), 'Save failed: ' + obj.get_path_name())
            saved.append(obj.get_path_name())
    require(definition.get_editor_property('ability_set'), 'Definition missing ability set')
    preserved = {name: row.get_editor_property(name) for name in
                 ('progression_tier', 'progression_kind', 'icon2d', 'item_mesh', 'rarity_tag', 'category_tag')}
    rows[tag] = unreal.ItemDefinition(weapon_definition=definition, **preserved)
    unreal.log('WEAPON_MIGRATED ' + tag_name + ' -> ' + definition.get_path_name())
registry.set_editor_property('item_definitions', rows)
require(unreal.EditorAssetLibrary.save_loaded_asset(registry, only_if_is_dirty=False), 'Registry save failed')
unreal.log('WEAPON_MIGRATION_SUCCESS saved=' + str(len(saved)) + ' profiles=4')
