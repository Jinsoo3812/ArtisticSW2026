"""Read-only Unreal Editor audit for the unified Strength combat pipeline.

Weapon direct damage always uses the native Strength execution. No assets are saved.
"""
import json
import math
from pathlib import Path
import unreal


def run():
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    registry.search_all_assets(synchronous_search=True)
    report = {"assets": [], "legacy_effect_referencers": [], "errors": []}
    def number(row, name, value, positive=False):
        row[name] = value
        if not math.isfinite(value) or (value <= 0 if positive else value < 0):
            report["errors"].append(f"{row['asset']}: invalid {name}={value}")
    roots = ["/Game/GameplayAbilitySystem/Weapon", "/Game/GameplayAbilitySystem/Enemy/Weapon",
             "/Game/GameplayAbilitySystem/Ability/Enemy"]
    for root in roots:
        for entry in registry.get_assets_by_path(root, recursive=True):
            path = str(entry.package_name)
            kind = str(entry.asset_class_path.asset_name)
            if kind not in {"Blueprint", "WeaponDataAsset"}:
                continue
            try:
                asset = entry.get_asset()
                row = {"asset": path}
                if isinstance(asset, unreal.WeaponDataAsset):
                    for i, definition in enumerate(asset.get_editor_property("weapon_definitions")):
                        number(row, f"weapon[{i}].strength_bonus", definition.get_editor_property("stats").get_editor_property("strength_bonus"))
                        number(row, f"weapon[{i}].attack_coefficient", definition.get_editor_property("combat_data").get_editor_property("attack_coefficient"), True)
                else:
                    cls = unreal.EditorAssetLibrary.load_blueprint_class(path)
                    if not cls:
                        raise RuntimeError("Blueprint class could not load")
                    defaults = unreal.get_default_object(cls)
                    if isinstance(defaults, unreal.BaseItem):
                        number(row, "strength_bonus", defaults.get_editor_property("strength_bonus"))
                    if isinstance(defaults, unreal.SwordItem):
                        number(row, "attack_coefficient", defaults.get_editor_property("attack_coefficient"), True)
                    if isinstance(defaults, unreal.ArrowProjectile):
                        data = defaults.get_editor_property("damage_data")
                        number(row, "attack_coefficient", data.get_editor_property("attack_coefficient"), True)
                        row["status_count"] = len(data.get_editor_property("status_effects"))
                    unreal.BlueprintEditorLibrary.compile_blueprint(asset)
                report["assets"].append(row)
            except Exception as error:
                report["errors"].append(f"{path}: {error}")
    options = unreal.AssetRegistryDependencyOptions(include_soft_package_references=True,
        include_hard_package_references=True, include_searchable_names=False,
        include_soft_management_references=False, include_hard_management_references=False)
    seen_effects = set()
    for entry in registry.get_assets_by_path("/Game/GameplayAbilitySystem/GameplayEffect", recursive=True):
        path = str(entry.package_name)
        if path in seen_effects:
            continue
        seen_effects.add(path)
        if any(word in path.lower() for word in ["arrow", "dashslash", "melee"]):
            report["legacy_effect_referencers"].append({"asset": path,
                "referencers": [str(x) for x in registry.get_referencers(entry.package_name, options)]})
    output = Path(unreal.Paths.project_saved_dir()) / "StrengthCombatAudit.json"
    output.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    unreal.log(f"Strength combat audit: {output}, errors={len(report['errors'])}")
    if report["errors"]:
        unreal.log_warning(str(report["errors"]))
    return report

run()
