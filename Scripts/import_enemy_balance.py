"""Import the versioned CSVs and assign editable BP defaults. Run in Unreal Python.

Does not save any map or player asset. Rerunning reimports CSVs and reapplies these
explicit defaults; use the DataTable Reimport button for data-only future edits.
"""
from pathlib import Path
import csv
import json
import unreal

ROOT = Path(unreal.Paths.project_dir()).resolve()
FOLDER = "/Game/GameplayAbilitySystem/Enemy/Data"
ENEMY = "/Game/GameplayAbilitySystem/Enemy"


def required(path):
    asset = unreal.load_asset(path)
    if not asset:
        raise RuntimeError(f"Missing asset: {path}")
    return asset


def save(asset):
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError(f"Failed to save {asset.get_path_name()}")


def import_table(name, struct_name, expected):
    source = ROOT / "DataTable" / f"{name}.csv"
    rows = list(csv.DictReader(source.open(encoding="utf-8-sig", newline="")))
    if len(rows) != expected or len({r['Name'] for r in rows}) != expected:
        raise RuntimeError(f"Wrong count or duplicate names: {source}")
    path = f"{FOLDER}/DT_{name}"
    row_type = unreal.load_object(None, f"/Script/Enemy.{struct_name}")
    if not row_type:
        raise RuntimeError(f"Build Enemy module first: {struct_name}")
    table = unreal.load_asset(path)
    if table is None:
        factory = unreal.DataTableFactory()
        factory.set_editor_property("struct", row_type)
        table = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            f"DT_{name}", FOLDER, unreal.DataTable, factory)
    if table.get_editor_property("row_struct") != row_type:
        raise RuntimeError(f"Wrong row structure: {path}")
    if not unreal.DataTableFunctionLibrary.fill_data_table_from_csv_file(table, str(source)):
        raise RuntimeError(f"CSV import failed: {source}")
    table.get_editor_property("asset_import_data").scripted_add_filename(str(source), 0, "Balance CSV")
    save(table)
    names = unreal.DataTableFunctionLibrary.get_data_table_row_names(table)
    if len(names) != expected:
        raise RuntimeError(f"Imported row count mismatch: {path}")
    return table


def handle(table, name):
    result = unreal.DataTableRowHandle()
    result.set_editor_property("data_table", table)
    result.set_editor_property("row_name", name)
    return result


def run():
    unreal.EditorAssetLibrary.make_directory(FOLDER)
    combat = import_table("EnemyCombatBalance", "EnemyCombatBalanceRow", 24)
    stats = import_table("EnemyBaseStats", "EnemyBaseStatsRow", 24)
    encounter = import_table("EnemyEncounterBalance", "EnemyEncounterBalanceRow", 4)
    assignments = {
        "BP_MeleeEnemy": "T1_Ground_Melee",
        "BP_RangedEnemy": "T1_Ground_Ranged",
        "BP_DeckMeleeEnemy": "T1_Deck_Melee",
        "BP_DeckRangedEnemy": "T1_Deck_Ranged",
        "BP_Ship_BossEnemy": "T1_Boss",
        "Bosses/BP_Boss_Mid_1": "T1_Boss",
        "Bosses/BP_Boss_Mid_2": "T2_Boss",
        "Bosses/BP_Boss_Mid_3": "T3_Boss",
        "Bosses/BP_Boss_Final": "T4_Boss",
    }
    ranged_class = unreal.EditorAssetLibrary.load_blueprint_class(f"{ENEMY}/BP_DeckRangedEnemy")
    audit = {"assignments": [], "weapons": [], "tables": [combat.get_path_name(), stats.get_path_name(), encounter.get_path_name()]}
    registries = {}
    for relative, row in assignments.items():
        bp = required(f"{ENEMY}/{relative}")
        cdo = unreal.get_default_object(bp.generated_class())
        cdo.set_editor_property("default_stats_row", handle(stats, row))
        if isinstance(cdo, unreal.ShipBossEnemy):
            cdo.set_editor_property("encounter_balance_row", handle(encounter, row.split('_')[0]))
            cdo.set_editor_property("summoned_enemy_class", ranged_class)
        unreal.BlueprintEditorLibrary.compile_blueprint(bp)
        save(bp)
        audit["assignments"].append({"asset": bp.get_path_name(), "row": row})
        # Record the effective weapon inputs so designers can verify the damage assumption.
        component = cdo.get_editor_property("weapon_component")
        try:
            registry = component.get_editor_property("weapon_registry")
            if registry:
                registries[registry.get_path_name()] = registry
        except Exception as error:
            audit.setdefault("notes", []).append(str(error))
    for path, registry in registries.items():
        definitions = list(registry.get_editor_property("weapon_definitions"))
        for definition in definitions:
            previous_bonus = definition.get_editor_property("stats").get_editor_property("strength_bonus")
            if path == f"{ENEMY}/Weapon/DA_Weapon.DA_Weapon":
                weapon_stats = definition.get_editor_property("stats")
                weapon_stats.set_editor_property("strength_bonus", 0.0)
                combat_data = definition.get_editor_property("combat_data")
                if combat_data.get_editor_property("attack_coefficient") != 1.0:
                    raise RuntimeError(f"Set enemy weapon coefficient to 1 in editor: {path}")
            audit["weapons"].append({
                "registry": path,
                "tag": str(definition.get_editor_property("weapon_tag").get_editor_property("tag_name")),
                "previous_strength_bonus": previous_bonus,
                "strength_bonus": definition.get_editor_property("stats").get_editor_property("strength_bonus"),
                "coefficient": definition.get_editor_property("combat_data").get_editor_property("attack_coefficient"),
            })
        if path == f"{ENEMY}/Weapon/DA_Weapon.DA_Weapon":
            for definition in registry.get_editor_property("weapon_definitions"):
                if definition.get_editor_property("stats").get_editor_property("strength_bonus") != 0.0:
                    raise RuntimeError("Enemy weapon bonus did not update")
            save(registry)
    arrow = required(f"{ENEMY}/Weapon/BP_EnemyProjectile")
    arrow_cdo = unreal.get_default_object(arrow.generated_class())
    damage_data = arrow_cdo.get_editor_property("damage_data")
    damage_data.set_editor_property("attack_coefficient", 1.0)
    arrow_cdo.set_editor_property("damage_data", damage_data)
    unreal.BlueprintEditorLibrary.compile_blueprint(arrow)
    save(arrow)
    output = ROOT / "Saved" / "EnemyBalanceImport.json"
    output.write_text(json.dumps(audit, ensure_ascii=False, indent=2), encoding="utf-8")
    unreal.log(f"ENEMY_BALANCE_IMPORTED tables=3 assignments={len(assignments)} audit={output}")


run()
