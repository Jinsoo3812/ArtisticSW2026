import unreal


ARCHETYPE_ROOT = "/Game/Blueprints/Ship/Enemy_Ship/Data/Archetype"
SKILL_MODULE_ROOT = "/Game/Blueprints/Ship/Enemy_Ship/Data/SkillModule"
NORMAL_VALUES = {
    "DA_ES_Normal_1": (1000.0, 3.0),
    "DA_ES_Normal_2": (1500.0, 2.5),
    "DA_ES_Normal_3": (2250.0, 2.0),
    "DA_ES_Normal_4": (3375.0, 1.5),
}


def save(asset):
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError(f"Unable to save {asset.get_path_name()}")


for path in unreal.EditorAssetLibrary.list_assets(
    ARCHETYPE_ROOT, recursive=True, include_folder=False
):
    asset = unreal.EditorAssetLibrary.load_asset(path)
    if not isinstance(asset, unreal.EnemyShipArchetypeData):
        continue
    speed, flight_time = NORMAL_VALUES.get(asset.get_name(), (1000.0, 3.0))
    profile = asset.get_editor_property("cannon_aim_profile")
    profile.trackable_target_speed = speed
    profile.projectile_flight_time = flight_time
    asset.set_editor_property("cannon_aim_profile", profile)
    save(asset)

# Resave affected assets so removed native properties are stripped.
for path in unreal.EditorAssetLibrary.list_assets(
    SKILL_MODULE_ROOT, recursive=True, include_folder=False
):
    asset = unreal.EditorAssetLibrary.load_asset(path)
    if isinstance(asset, unreal.EnemyShipSkillModuleData):
        save(asset)

cannon_ability = unreal.EditorAssetLibrary.load_asset(
    "/Game/Blueprints/Ship/Enemy_Ship/GA/BP_GA_ES_CannonVolley"
)
if cannon_ability:
    unreal.BlueprintEditorLibrary.compile_blueprint(cannon_ability)
    save(cannon_ability)

unreal.log("[ES-CANNON-AIM] Archetypes and cannon assets migrated")
