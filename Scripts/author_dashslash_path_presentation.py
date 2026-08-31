import unreal


CUE_FOLDER = "/Game/GameplayCues/Path/Boss"
DATA_FOLDER = "/Game/GameplayAbilitySystem/Enemy/DA"
PRESENTATION_PATH = f"{DATA_FOLDER}/DA_DashSlashPathPresentation"
DASH_ABILITY_PATH = "/Game/GameplayAbilitySystem/Ability/Enemy/Boss/BPGA_SlashDash"
PATH_CUE_PARENT = "/Script/ArtisticSWCore.SWPathGameplayCueNotify"
TELEGRAPH_EFFECT = "/Script/Enemy.BossDashSlashTelegraphEffect"
EXECUTION_EFFECT = "/Script/Enemy.BossDashSlashExecutionPathEffect"


def require(value, label):
    if value is None:
        raise RuntimeError(f"Missing {label}")
    return value


def gameplay_tag(tag_name):
    tag = unreal.GameplayTag()
    if not tag.import_text(tag_name):
        raise RuntimeError(f"Could not import GameplayTag: {tag_name}")
    return tag


def create_or_update_path_cue(asset_name, tag_name):
    asset_path = f"{CUE_FOLDER}/{asset_name}"
    blueprint = unreal.EditorAssetLibrary.load_asset(asset_path)
    if blueprint is None:
        factory = unreal.BlueprintFactory()
        factory.set_editor_property(
            "parent_class",
            require(unreal.load_class(None, PATH_CUE_PARENT), "path cue parent class"),
        )
        blueprint = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            asset_name, CUE_FOLDER, unreal.Blueprint, factory
        )
    require(blueprint, asset_path)

    cdo = unreal.get_default_object(blueprint.generated_class())
    cdo.set_editor_property("gameplay_cue_tag", gameplay_tag(tag_name))
    cdo.set_editor_property("auto_destroy_on_remove", True)
    cdo.set_editor_property("auto_attach_to_owner", False)
    cdo.set_editor_property("unique_instance_per_instigator", True)
    if not unreal.EditorAssetLibrary.save_loaded_asset(blueprint, only_if_is_dirty=False):
        raise RuntimeError(f"Could not save {asset_path}")
    return blueprint


def create_or_update_presentation():
    presentation = unreal.EditorAssetLibrary.load_asset(PRESENTATION_PATH)
    if presentation is None:
        factory = unreal.DataAssetFactory()
        factory.set_editor_property("data_asset_class", unreal.PathCombatPresentationDataAsset)
        presentation = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            "DA_DashSlashPathPresentation",
            DATA_FOLDER,
            unreal.PathCombatPresentationDataAsset,
            factory,
        )
    require(presentation, PRESENTATION_PATH)
    presentation.set_editor_property(
        "telegraph_effect_class",
        require(unreal.load_class(None, TELEGRAPH_EFFECT), "telegraph effect class"),
    )
    presentation.set_editor_property(
        "execution_effect_class",
        require(unreal.load_class(None, EXECUTION_EFFECT), "execution effect class"),
    )
    if not unreal.EditorAssetLibrary.save_loaded_asset(presentation, only_if_is_dirty=False):
        raise RuntimeError(f"Could not save {PRESENTATION_PATH}")
    return presentation


def assign_presentation_to_dash_ability(presentation):
    blueprint = require(
        unreal.EditorAssetLibrary.load_asset(DASH_ABILITY_PATH), DASH_ABILITY_PATH
    )
    cdo = unreal.get_default_object(blueprint.generated_class())
    cdo.set_editor_property("path_presentation", presentation)
    if not unreal.EditorAssetLibrary.save_loaded_asset(blueprint, only_if_is_dirty=False):
        raise RuntimeError(f"Could not save {DASH_ABILITY_PATH}")


def main():
    create_or_update_path_cue(
        "GCN_Path_Boss_DashSlash_Telegraph",
        "GameplayCue.Path.Boss.DashSlash.Telegraph",
    )
    create_or_update_path_cue(
        "GCN_Path_Boss_DashSlash_Execution",
        "GameplayCue.Path.Boss.DashSlash.Execution",
    )
    presentation = create_or_update_presentation()
    assign_presentation_to_dash_ability(presentation)
    unreal.log("DashSlash path presentation assets authored successfully.")


if __name__ == "__main__":
    main()
