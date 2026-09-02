import copy
import json
import math

import unreal


DATA_ROOT = "/Game/Blueprints/Ship/Enemy_Ship/Data"
FLEET_ROOT = DATA_ROOT + "/Archetype/EnemyFleet"
NORMAL_ROOT = FLEET_ROOT + "/Normal"
SKILL_ROOT = FLEET_ROOT + "/Skill"
STAT_TABLE_PATH = "/Game/Blueprints/Ship/Data/DT_ShipStat"
BASE_ARCHETYPE_PATH = DATA_ROOT + "/Archetype/DA_ES_Archetype_Cannon"
MODULE_ROOT = DATA_ROOT + "/SkillModule"


def load(path):
    asset = unreal.EditorAssetLibrary.load_asset(path)
    if not asset:
        raise RuntimeError(f"Unable to load {path}")
    return asset


def save(asset):
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError(f"Unable to save {asset.get_path_name()}")


def round_positive(value):
    return int(math.floor(float(value) + 0.5))


def get_or_duplicate_archetype(asset_name, folder, source_path):
    path = f"{folder}/{asset_name}"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        return load(path)
    if not unreal.EditorAssetLibrary.duplicate_asset(source_path, path):
        raise RuntimeError(f"Unable to create {path}")
    return load(path)


def configure_archetype(asset, stat_table, row_name, modules, template):
    spec_row = asset.get_editor_property("spec_row")
    spec_row.data_table = stat_table
    spec_row.row_name = row_name
    asset.set_editor_property("spec_row", spec_row)
    asset.set_editor_property(
        "navigation_profile", template.get_editor_property("navigation_profile")
    )
    asset.set_editor_property(
        "zero_health_cannon_cooldown_multiplier",
        template.get_editor_property("zero_health_cannon_cooldown_multiplier"),
    )
    asset.set_editor_property(
        "selection_policy", template.get_editor_property("selection_policy")
    )
    asset.set_editor_property("skill_modules", modules)
    save(asset)


stat_table = load(STAT_TABLE_PATH)
base_archetype = load(BASE_ARCHETYPE_PATH)
base_row_name = str(base_archetype.get_editor_property("spec_row").row_name)
rows = json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(stat_table))
base_row = next((row for row in rows if row.get("Name") == base_row_name), None)
if not base_row:
    raise RuntimeError(f"Baseline row {base_row_name} is missing from {STAT_TABLE_PATH}")

required_fields = (
    "MaxHealth",
    "ForwardPropulsionMultiplier",
    "TurnTorqueMultiplier",
    "CannonDamage",
    "CannonFireCooldown",
)
for field in required_fields:
    if field not in base_row:
        raise RuntimeError(f"Baseline row is missing {field}: {base_row}")

generated_row_names = []
for tier in range(1, 5):
    scale = 1.5 ** (tier - 1)
    row_name = f"EnemyShip_Normal_{tier}"
    generated_row_names.append(row_name)
    generated = copy.deepcopy(base_row)
    generated["Name"] = row_name
    generated["MaxHealth"] = round_positive(base_row["MaxHealth"] * scale)
    generated["CannonDamage"] = round_positive(base_row["CannonDamage"] * scale)
    generated["ForwardPropulsionMultiplier"] = round_positive(
        base_row["ForwardPropulsionMultiplier"] * scale
    )
    generated["TurnTorqueMultiplier"] = round_positive(
        base_row["TurnTorqueMultiplier"] * scale
    )
    # Harder ships fire faster. Cooldown deliberately remains fractional.
    generated["CannonFireCooldown"] = base_row["CannonFireCooldown"] / scale

    existing_index = next(
        (index for index, row in enumerate(rows) if row.get("Name") == row_name),
        None,
    )
    if existing_index is None:
        rows.append(generated)
    else:
        rows[existing_index] = generated

if not unreal.DataTableFunctionLibrary.fill_data_table_from_json_string(
    stat_table, json.dumps(rows)
):
    raise RuntimeError(f"Unable to update {STAT_TABLE_PATH}")
save(stat_table)

unreal.EditorAssetLibrary.make_directory(NORMAL_ROOT)
unreal.EditorAssetLibrary.make_directory(SKILL_ROOT)

modules = {
    name: load(f"{MODULE_ROOT}/DA_ES_SkillModule_{name}")
    for name in ("Cannon", "Charge", "Obstacle", "TimeStop", "Torpedo")
}

for tier, row_name in enumerate(generated_row_names, start=1):
    archetype = get_or_duplicate_archetype(
        f"DA_ES_Normal_{tier}", NORMAL_ROOT, BASE_ARCHETYPE_PATH
    )
    configure_archetype(
        archetype, stat_table, row_name, [modules["Cannon"]], base_archetype
    )

for skill_name in ("Charge", "Obstacle", "TimeStop", "Torpedo"):
    source_path = f"{DATA_ROOT}/Archetype/DA_ES_Archetype_{skill_name}"
    source_archetype = load(source_path)
    archetype = get_or_duplicate_archetype(
        f"DA_ES_{skill_name}", SKILL_ROOT, source_path
    )
    configure_archetype(
        archetype,
        stat_table,
        base_row_name,
        [modules["Cannon"], modules[skill_name]],
        source_archetype,
    )

unreal.log(
    f"[ES-FLEET] Created/updated fleet assets in {FLEET_ROOT}; "
    f"baseline={base_row_name}"
)
