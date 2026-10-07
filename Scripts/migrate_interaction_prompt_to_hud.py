import unreal


PROMPT_CLASS_PATH = (
    "/Game/Blueprints/02_UI/UI_Interact/"
    "WBP_InteractPrompt.WBP_InteractPrompt_C"
)

CANDIDATE_BLUEPRINTS = [
    "/Game/Blueprints/Interactable_Object/BP_WorkTable",
    "/Game/Blueprints/Item/BP/LootSpawn/BP_LooseLootItem",
    "/Game/Blueprints/Item/Legacy/BP_StorageChest",
    "/Game/Blueprints/Item/Legacy/Item_Material/BP_Item_TestCloth",
    "/Game/Blueprints/Item/Legacy/Item_Material/BP_Item_TestOre",
    "/Game/Blueprints/Item/Legacy/Item_Material/BP_Item_TestWoods",
    "/Game/Blueprints/Item/Legacy/Item_Submunition/BP_Material_BombTrap",
    "/Game/Blueprints/Item/Legacy/Item_Submunition/BP_Material_MiniBomb",
    "/Game/Blueprints/Item/Legacy/Item_Tool/BP_Item_ClusterGrenade",
    "/Game/Blueprints/Item/Legacy/Item_Tool/BP_Item_SniperRifle",
    "/Game/Blueprints/Item/Legacy/Item_Tool/BP_Item_TestBlue",
    "/Game/Blueprints/Item/Legacy/Item_Tool/BP_Item_TestGrenade",
    "/Game/Blueprints/Item/Legacy/Item_Tool/BP_Item_TestRed",
    "/Game/Blueprints/Item/Legacy/Item_Tool/BP_Item_Trap",
]


def remove_prompt_components(asset_path):
    blueprint = unreal.EditorAssetLibrary.load_asset(asset_path)
    if blueprint is None:
        raise RuntimeError(f"Failed to load {asset_path}")

    subsystem = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    handles = subsystem.k2_gather_subobject_data_for_blueprint(blueprint)
    context_handle = None
    prompt_handles = []
    prompt_objects = []

    for handle in handles:
        data = unreal.SubobjectDataBlueprintFunctionLibrary.get_data(handle)
        if unreal.SubobjectDataBlueprintFunctionLibrary.is_root_actor(data):
            context_handle = handle

        obj = unreal.SubobjectDataBlueprintFunctionLibrary.get_object(data)
        if not isinstance(obj, unreal.WidgetComponent):
            continue

        widget_class = obj.get_editor_property("widget_class")
        if widget_class and widget_class.get_path_name() == PROMPT_CLASS_PATH:
            if obj in prompt_objects:
                continue
            if not unreal.SubobjectDataBlueprintFunctionLibrary.can_delete(data):
                raise RuntimeError(
                    f"Prompt component cannot be deleted: {obj.get_path_name()}"
                )
            prompt_objects.append(obj)
            prompt_handles.append(handle)

    if not prompt_handles:
        return 0
    if context_handle is None:
        raise RuntimeError(f"No root actor handle found for {asset_path}")

    removed_count = subsystem.delete_subobjects(
        context_handle, prompt_handles, blueprint
    )
    if removed_count != len(prompt_handles):
        raise RuntimeError(
            f"Expected to remove {len(prompt_handles)} prompt components from "
            f"{asset_path}, removed {removed_count}"
        )

    unreal.EditorAssetLibrary.save_loaded_asset(blueprint, only_if_is_dirty=False)
    unreal.log(
        f"[InteractionPromptMigration] Removed {removed_count} world prompt "
        f"component(s) from {asset_path}"
    )
    return removed_count


if __name__ == "__main__":
    total_removed = 0
    for path in CANDIDATE_BLUEPRINTS:
        total_removed += remove_prompt_components(path)

    unreal.log(
        f"[InteractionPromptMigration] Removed {total_removed} world prompt "
        "component(s); the HUD now owns the shared prompt."
    )
