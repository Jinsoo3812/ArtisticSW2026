import unreal


ROOT = "/Game/Blueprints/Ship/Enemy_Ship/Telegraph"
EXPECTED_ASSETS = [
    ROOT + "/T_ES_ChargeArrow_Test",
    ROOT + "/SM_ES_ChargeTelegraphPlane",
    ROOT + "/M_ES_ChargeTelegraph",
    ROOT + "/BP_ES_ChargeTelegraph",
]
ABILITY = "/Game/Blueprints/Ship/Enemy_Ship/GA/BP_GA_ES_Charge"


def check(condition, message):
    if not condition:
        raise RuntimeError(message)
    unreal.log("VERIFY_OK: " + message)


for asset_path in EXPECTED_ASSETS:
    check(unreal.EditorAssetLibrary.does_asset_exist(asset_path), "asset exists: " + asset_path)

material = unreal.EditorAssetLibrary.load_asset(ROOT + "/M_ES_ChargeTelegraph")
check(material.get_editor_property("blend_mode") == unreal.BlendMode.BLEND_TRANSLUCENT,
      "material is translucent")
check(material.get_editor_property("shading_model") == unreal.MaterialShadingModel.MSM_UNLIT,
      "material is unlit")
check(unreal.MaterialEditingLibrary.get_material_property_input_node(
      material, unreal.MaterialProperty.MP_EMISSIVE_COLOR) is not None,
      "material has an authored emissive-color graph")
check(unreal.MaterialEditingLibrary.get_material_property_input_node(
      material, unreal.MaterialProperty.MP_OPACITY) is not None,
      "material has an authored opacity graph")
check(abs(unreal.MaterialEditingLibrary.get_material_default_scalar_parameter_value(
      material, "ArrowScrollSpeed") + 0.8) < 0.01,
      "material exposes Time-driven ArrowScrollSpeed")
check(unreal.MaterialEditingLibrary.get_material_default_texture_parameter_value(
      material, "ArrowMask").get_path_name().startswith(ROOT + "/T_ES_ChargeArrow_Test"),
      "material exposes a replaceable texture mask")

telegraph_class = unreal.EditorAssetLibrary.load_blueprint_class(ROOT + "/BP_ES_ChargeTelegraph")
check(telegraph_class is not None, "telegraph Blueprint generated class loads")
telegraph_cdo = unreal.get_default_object(telegraph_class)
check(telegraph_cdo.get_editor_property("warning_material").get_path_name().startswith(
      ROOT + "/M_ES_ChargeTelegraph"), "telegraph Blueprint uses generated material")
check(telegraph_cdo.get_editor_property("warning_plane").get_editor_property(
      "static_mesh").get_path_name().startswith(ROOT + "/SM_ES_ChargeTelegraphPlane"),
      "telegraph Blueprint uses project-local Plane copy")

ability_class = unreal.EditorAssetLibrary.load_blueprint_class(ABILITY)
check(ability_class is not None, "charge ability Blueprint generated class loads")
ability_cdo = unreal.get_default_object(ability_class)
check(abs(ability_cdo.get_editor_property("charge_distance") - 10000.0) < 0.01,
      "charge distance is 10000 cm")
check(abs(ability_cdo.get_editor_property("charge_propulsion_multiplier") - 2.0) < 0.01,
      "charge propulsion scale is 2x")
check(abs(ability_cdo.get_editor_property("charge_failsafe_duration_seconds")) < 0.01,
      "charge has no time-based normal termination")
check(abs(ability_cdo.get_editor_property("charge_telegraph_width") - 1000.0) < 0.01,
      "telegraph width is 1000 cm")
check(ability_cdo.get_editor_property("charge_telegraph_class") == telegraph_class,
      "charge ability references telegraph Blueprint")

referencers = set(unreal.EditorAssetLibrary.find_package_referencers_for_asset(ABILITY, False))
check("/Game/Blueprints/Ship/Enemy_Ship/Data/AbilitySet/DA_ES_AbilitySet_Charge" in referencers,
      "existing Charge AbilitySet still references BP_GA_ES_Charge")
check(unreal.EditorAssetLibrary.does_asset_exist(
      "/Game/Blueprints/Ship/Enemy_Ship/Data/Archetype/DA_ES_Archetype_Charge"),
      "Charge archetype remains available for BP_EnemyShip testing")

unreal.log("CODEX_CHARGE_TELEGRAPH_VERIFIED")
