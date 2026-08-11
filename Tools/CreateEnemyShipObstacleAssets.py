import unreal


ROOT = "/Game/New/Enemy_Ship"


def load(path):
    asset = unreal.load_asset(path)
    if not asset:
        raise RuntimeError(f"Required asset is missing: {path}")
    return asset


def create_blueprint(asset_name, package_path, parent_class):
    asset_path = f"{package_path}/{asset_name}"
    existing = unreal.load_asset(asset_path)
    if existing:
        return existing
    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", parent_class)
    asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        asset_name, package_path, unreal.Blueprint, factory)
    if not asset:
        raise RuntimeError(f"Failed to create Blueprint: {asset_path}")
    return asset


def duplicate(source_path, destination_path):
    asset = unreal.load_asset(destination_path)
    if asset:
        return asset
    if not unreal.EditorAssetLibrary.duplicate_asset(source_path, destination_path):
        raise RuntimeError(f"Failed to duplicate {source_path} -> {destination_path}")
    return load(destination_path)


def generated_class(blueprint):
    result = blueprint.generated_class()
    if not result:
        raise RuntimeError(f"Blueprint has no generated class: {blueprint.get_path_name()}")
    return result


obstacle_bp = create_blueprint(
    "BP_ES_Obstacle",
    f"{ROOT}/Blueprints",
    unreal.EnemyShipObstacle.static_class())
carrier_bp = create_blueprint(
    "BP_ES_ObstacleProjectile",
    f"{ROOT}/Blueprints",
    unreal.EnemyShipObstacleProjectile.static_class())
ability_bp = create_blueprint(
    "BP_GA_ES_DeployObstacle",
    f"{ROOT}/GA",
    unreal.GA_EnemyShipDeployObstacle.static_class())

unreal.BlueprintEditorLibrary.compile_blueprint(obstacle_bp)
unreal.BlueprintEditorLibrary.compile_blueprint(carrier_bp)
unreal.BlueprintEditorLibrary.compile_blueprint(ability_bp)

obstacle_class = generated_class(obstacle_bp)
carrier_class = generated_class(carrier_bp)
ability_class = generated_class(ability_bp)
ability_cdo = unreal.get_default_object(ability_class)
ability_cdo.set_editor_property("obstacle_projectile_class", carrier_class)
ability_cdo.set_editor_property("obstacle_class", obstacle_class)

ability_set = duplicate(
    f"{ROOT}/Data/AbilitySet/DA_ES_AbilitySet_Torpedo",
    f"{ROOT}/Data/AbilitySet/DA_ES_AbilitySet_Obstacle")
ability_set.set_editor_property("abilities", [ability_class])

skill_module = duplicate(
    f"{ROOT}/Data/SkillModule/DA_ES_SkillModule_Torpedo",
    f"{ROOT}/Data/SkillModule/DA_ES_SkillModule_Obstacle")
skill_module.set_editor_property("module_id", "Skill.Obstacle")
skill_module.set_editor_property("ability_set", ability_set)
rules = skill_module.get_editor_property("skill_rules")
if len(rules) != 1:
    raise RuntimeError("The Torpedo template SkillModule must contain exactly one rule")
rule = rules[0]
rule.set_editor_property("rule_id", "Skill.Obstacle.Use")
rule.set_editor_property(
    "ability_tag",
    unreal.GA_EnemyShipDeployObstacle.get_deploy_obstacle_ability_tag())
rule.set_editor_property("ability_class", ability_class)
skill_module.set_editor_property("skill_rules", [rule])

pattern = duplicate(
    f"{ROOT}/Data/Pattern/DA_ES_Pattern_Torpedo",
    f"{ROOT}/Data/Pattern/DA_ES_Pattern_Obstacle")
cannon_module = load(f"{ROOT}/Data/SkillModule/DA_ES_SkillModule_Cannon")
pattern.set_editor_property("skill_modules", [cannon_module, skill_module])

archetype = duplicate(
    f"{ROOT}/Data/Archetype/DA_ES_Archetype_Torpedo",
    f"{ROOT}/Data/Archetype/DA_ES_Archetype_Obstacle")
archetype.set_editor_property("pattern", pattern)

for asset in (
    obstacle_bp,
    carrier_bp,
    ability_bp,
    ability_set,
    skill_module,
    pattern,
    archetype,
):
    unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False)

saved_rule = skill_module.get_editor_property("skill_rules")[0]
saved_tag = saved_rule.get_editor_property("ability_tag").get_editor_property("tag_name")
if str(saved_tag) != "GameplayAbility.EnemyShip.DeployObstacle":
    raise RuntimeError(f"Unexpected obstacle ability tag: {saved_tag}")
if skill_module.get_editor_property("ability_set") != ability_set:
    raise RuntimeError("Obstacle SkillModule is not linked to its AbilitySet")
if pattern.get_editor_property("skill_modules")[-1] != skill_module:
    raise RuntimeError("Obstacle Pattern is not linked to its SkillModule")
if archetype.get_editor_property("pattern") != pattern:
    raise RuntimeError("Obstacle Archetype is not linked to its Pattern")
if ability_cdo.get_editor_property("obstacle_projectile_class") != carrier_class:
    raise RuntimeError("Obstacle Ability does not use the carrier Blueprint class")
if ability_cdo.get_editor_property("obstacle_class") != obstacle_class:
    raise RuntimeError("Obstacle Ability does not use the obstacle Blueprint class")

unreal.log_warning("[EnemyShipObstacleAssets] Created and linked Blueprint, AbilitySet, SkillModule, Pattern, and Archetype assets.")
