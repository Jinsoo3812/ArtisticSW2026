import unreal


ROOT = "/Game/Blueprints/Ship/Enemy_Ship/Data"
MODULE_ROOT = ROOT + "/SkillModule"
ARCHETYPE_ROOT = ROOT + "/Archetype"
STAT_TABLE = unreal.EditorAssetLibrary.load_asset("/Game/Blueprints/Ship/Data/DT_ShipStat")


def load(path):
    asset = unreal.EditorAssetLibrary.load_asset(path)
    if not asset:
        raise RuntimeError(f"Unable to load {path}")
    return asset


def save(asset):
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError(f"Unable to save {asset.get_path_name()}")


module_specs = {
    "Cannon": {
        "ability": "/Game/Blueprints/Ship/Enemy_Ship/GA/BP_GA_ES_CannonVolley.BP_GA_ES_CannonVolley_C",
        "states": [unreal.NavalCombatState.ORBIT], "prediction": 0.5, "priority": 10,
        "movement": unreal.EnemyShipSkillMovementPolicy.CONTINUE_NAVIGATION,
    },
    "Charge": {
        "ability": "/Game/Blueprints/Ship/Enemy_Ship/GA/BP_GA_ES_Charge.BP_GA_ES_Charge_C",
        "states": [], "prediction": 0.0, "priority": 10,
        "movement": unreal.EnemyShipSkillMovementPolicy.OVERRIDE_NAVIGATION,
    },
    "Obstacle": {
        "ability": "/Game/Blueprints/Ship/Enemy_Ship/GA/BP_GA_ES_DeployObstacle.BP_GA_ES_DeployObstacle_C",
        "states": [], "prediction": 0.0, "priority": 100,
        "movement": unreal.EnemyShipSkillMovementPolicy.CONTINUE_NAVIGATION,
    },
    "TimeStop": {
        "ability": "/Game/Blueprints/Ship/Enemy_Ship/GA/BP_GA_ES_TimeStop.BP_GA_ES_TimeStop_C",
        "states": [], "prediction": 0.0, "priority": 100,
        "movement": unreal.EnemyShipSkillMovementPolicy.CONTINUE_NAVIGATION,
    },
    "Torpedo": {
        "ability": "/Game/Blueprints/Ship/Enemy_Ship/GA/BP_GA_ESLaunchTorpedo.BP_GA_ESLaunchTorpedo_C",
        "states": [], "prediction": 0.0, "priority": 100,
        "movement": unreal.EnemyShipSkillMovementPolicy.CONTINUE_NAVIGATION,
    },
}

modules = {}
for name, spec in module_specs.items():
    module = load(f"{MODULE_ROOT}/DA_ES_SkillModule_{name}")
    module.set_editor_property("ability_class", unreal.load_class(None, spec["ability"]))
    module.set_editor_property("required_owner_tags", unreal.GameplayTagContainer())
    module.set_editor_property("blocked_owner_tags", unreal.GameplayTagContainer())
    module.set_editor_property("allowed_navigation_states", spec["states"])
    module.set_editor_property("target_prediction_strength", spec["prediction"])
    module.set_editor_property("priority", spec["priority"])
    module.set_editor_property("weight", 1.0)
    module.set_editor_property("use_only_once", False)
    module.set_editor_property("movement_policy", spec["movement"])
    save(module)
    modules[name] = module

# Reserialize the table against the new FShipStatRow schema so the removed
# ShipSpeedMultiplier column is stripped while all current row values remain.
save(STAT_TABLE)


far_nav = {
    "detection_distance": 30000.0, "ideal_distance": 20000.0,
    "orbit_tolerance": 5000.0, "danger_close_distance": 5000.0,
    "return_arrival_distance": 800.0, "return_trigger_distance": 30000.0,
    "return_propulsion_multiplier": 3.0, "lost_target_return_delay": 10.0,
    "orbit_clockwise": True, "avoidance_decision_interval": 0.1,
    "avoidance_safety_buffer": 800.0,
}

standard_nav = {
    "detection_distance": 12000.0, "ideal_distance": 9000.0,
    "orbit_tolerance": 1500.0, "danger_close_distance": 5000.0,
    "return_arrival_distance": 800.0, "return_trigger_distance": 800.0,
    "return_propulsion_multiplier": 1.0, "lost_target_return_delay": 10.0,
    "orbit_clockwise": True, "avoidance_decision_interval": 0.1,
    "avoidance_safety_buffer": 800.0,
}

archetype_specs = {
    "DA_ES_Archetype_Cannon": (far_nav, ["Cannon"]),
    "DA_ES_Archetype_Charge": (standard_nav, ["Cannon", "Charge"]),
    "DA_ES_Archetype_Obstacle": (standard_nav, ["Cannon", "Obstacle"]),
    "DA_ES_Archetype_TimeStop": (standard_nav, ["Cannon", "TimeStop"]),
    "DA_ES_Archetype_Torpedo": (standard_nav, ["Cannon", "Torpedo"]),
    "Normal/DA_ES_Archetype_Easy_Close": (far_nav, ["Cannon"]),
    "Normal/DA_ES_Archetype_Easy_Far": (far_nav, ["Cannon"]),
    "Normal/DA_ES_Archetype_Easy_Mid": ({**far_nav, "orbit_clockwise": False}, ["Cannon"]),
}

for relative_name, (nav_values, module_names) in archetype_specs.items():
    archetype = load(f"{ARCHETYPE_ROOT}/{relative_name}")
    row = archetype.get_editor_property("spec_row")
    row.data_table = STAT_TABLE
    row.row_name = "EnemyShip_Easy"
    archetype.set_editor_property("spec_row", row)
    nav = archetype.get_editor_property("navigation_profile")
    for key, value in nav_values.items():
        setattr(nav, key, value)
    archetype.set_editor_property("navigation_profile", nav)
    archetype.set_editor_property("orbit_distance_spacing", 3000.0)
    archetype.set_editor_property("zero_health_cannon_cooldown_multiplier", 3.0)
    archetype.set_editor_property("selection_policy", unreal.EnemyShipSkillSelectionPolicy.HIGHEST_PRIORITY)
    archetype.set_editor_property("skill_modules", [modules[name] for name in module_names])
    save(archetype)


# Resave the Blueprint so removed legacy properties are stripped from its defaults.
ship_bp = load("/Game/Blueprints/Ship/Enemy_Ship/Blueprints/BP_EnemyShip")
save(ship_bp)

unreal.log("[ES-MIGRATE] Three-stage enemy ship data migration completed; obsolete assets retained for safe cleanup")
