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


def create_laser_material():
    asset_path = f"{ROOT}/Materials/M_ES_TimeStopLaser"
    existing = unreal.load_asset(asset_path)
    if existing:
        return existing

    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        "M_ES_TimeStopLaser",
        f"{ROOT}/Materials",
        unreal.Material,
        unreal.MaterialFactoryNew())
    if not material:
        raise RuntimeError(f"Failed to create laser material: {asset_path}")

    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("two_sided", True)

    color = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionVectorParameter, -500, -100)
    color.set_editor_property("parameter_name", "LaserColor")
    color.set_editor_property("default_value", unreal.LinearColor(1.0, 0.01, 0.01, 1.0))

    emissive_strength = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionScalarParameter, -500, 100)
    emissive_strength.set_editor_property("parameter_name", "EmissiveStrength")
    emissive_strength.set_editor_property("default_value", 15.0)

    emissive = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionMultiply, -220, -50)
    unreal.MaterialEditingLibrary.connect_material_expressions(color, "", emissive, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(
        emissive_strength, "", emissive, "B")
    unreal.MaterialEditingLibrary.connect_material_property(
        emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    opacity = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionScalarParameter, -220, 180)
    opacity.set_editor_property("parameter_name", "LaserOpacity")
    opacity.set_editor_property("default_value", 0.3)
    unreal.MaterialEditingLibrary.connect_material_property(
        opacity, "", unreal.MaterialProperty.MP_OPACITY)

    unreal.MaterialEditingLibrary.recompile_material(material)
    return material


laser_material = create_laser_material()
projectile_bp = create_blueprint(
    "BP_ES_TimeStopProjectile",
    f"{ROOT}/Blueprints",
    unreal.EnemyShipTimeStopProjectile.static_class())
field_bp = create_blueprint(
    "BP_ES_TimeStopField",
    f"{ROOT}/Blueprints",
    unreal.EnemyShipTimeStopField.static_class())
aim_line_bp = create_blueprint(
    "BP_ES_TimeStopAimLine",
    f"{ROOT}/Blueprints",
    unreal.EnemyShipTimeStopAimLine.static_class())
ability_bp = create_blueprint(
    "BP_GA_ES_TimeStop",
    f"{ROOT}/GA",
    unreal.GA_EnemyShipTimeStop.static_class())

for blueprint in (projectile_bp, field_bp, aim_line_bp, ability_bp):
    unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)

projectile_class = generated_class(projectile_bp)
field_class = generated_class(field_bp)
aim_line_class = generated_class(aim_line_bp)
ability_class = generated_class(ability_bp)
aim_line_cdo = unreal.get_default_object(aim_line_class)
aim_line_cdo.set_editor_property("laser_material", laser_material)
ability_cdo = unreal.get_default_object(ability_class)
ability_cdo.set_editor_property("projectile_class", projectile_class)
ability_cdo.set_editor_property("field_class", field_class)
ability_cdo.set_editor_property("aim_line_class", aim_line_class)

ability_set = duplicate(
    f"{ROOT}/Data/AbilitySet/DA_ES_AbilitySet_Torpedo",
    f"{ROOT}/Data/AbilitySet/DA_ES_AbilitySet_TimeStop")
ability_set.set_editor_property("abilities", [ability_class])

skill_module = duplicate(
    f"{ROOT}/Data/SkillModule/DA_ES_SkillModule_Torpedo",
    f"{ROOT}/Data/SkillModule/DA_ES_SkillModule_TimeStop")
skill_module.set_editor_property("module_id", "Skill.TimeStop")
skill_module.set_editor_property("ability_set", ability_set)
rules = skill_module.get_editor_property("skill_rules")
if len(rules) != 1:
    raise RuntimeError("The Torpedo template SkillModule must contain exactly one rule")
rule = rules[0]
rule.set_editor_property("rule_id", "Skill.TimeStop.Use")
rule.set_editor_property(
    "ability_tag", unreal.GA_EnemyShipTimeStop.get_time_stop_ability_tag())
rule.set_editor_property("ability_class", ability_class)
skill_module.set_editor_property("skill_rules", [rule])

pattern = duplicate(
    f"{ROOT}/Data/Pattern/DA_ES_Pattern_Torpedo",
    f"{ROOT}/Data/Pattern/DA_ES_Pattern_TimeStop")
cannon_module = load(f"{ROOT}/Data/SkillModule/DA_ES_SkillModule_Cannon")
pattern.set_editor_property("skill_modules", [cannon_module, skill_module])

archetype = duplicate(
    f"{ROOT}/Data/Archetype/DA_ES_Archetype_Torpedo",
    f"{ROOT}/Data/Archetype/DA_ES_Archetype_TimeStop")
archetype.set_editor_property("pattern", pattern)

for asset in (
    projectile_bp,
    field_bp,
    aim_line_bp,
    laser_material,
    ability_bp,
    ability_set,
    skill_module,
    pattern,
    archetype,
):
    unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False)

saved_rule = skill_module.get_editor_property("skill_rules")[0]
saved_tag = saved_rule.get_editor_property("ability_tag").get_editor_property("tag_name")
if str(saved_tag) != "GameplayAbility.EnemyShip.TimeStop":
    raise RuntimeError(f"Unexpected Time Stop ability tag: {saved_tag}")
if skill_module.get_editor_property("ability_set") != ability_set:
    raise RuntimeError("Time Stop SkillModule is not linked to its AbilitySet")
if pattern.get_editor_property("skill_modules")[-1] != skill_module:
    raise RuntimeError("Time Stop Pattern is not linked to its SkillModule")
if archetype.get_editor_property("pattern") != pattern:
    raise RuntimeError("Time Stop Archetype is not linked to its Pattern")
if ability_cdo.get_editor_property("projectile_class") != projectile_class:
    raise RuntimeError("Time Stop Ability does not use the projectile Blueprint class")
if ability_cdo.get_editor_property("field_class") != field_class:
    raise RuntimeError("Time Stop Ability does not use the field Blueprint class")
if ability_cdo.get_editor_property("aim_line_class") != aim_line_class:
    raise RuntimeError("Time Stop Ability does not use the aim-line Blueprint class")
if aim_line_cdo.get_editor_property("laser_material") != laser_material:
    raise RuntimeError("Time Stop Aim Line does not use the translucent red laser material")

unreal.log_warning(
    "[EnemyShipTimeStopAssets] Created and linked Blueprint, AbilitySet, "
    "SkillModule, Pattern, and Archetype assets.")
