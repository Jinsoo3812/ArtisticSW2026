import unreal
import os
import sys

sys.path.append(os.path.dirname(__file__))
from migrate_interaction_prompt_to_hud import CANDIDATE_BLUEPRINTS, PROMPT_CLASS_PATH


for asset_path in CANDIDATE_BLUEPRINTS:
    blueprint = unreal.EditorAssetLibrary.load_asset(asset_path)
    if blueprint is None:
        raise RuntimeError(f"Failed to load {asset_path}")

    subsystem = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    handles = subsystem.k2_gather_subobject_data_for_blueprint(blueprint)
    objects = []
    interaction_count = 0
    for handle in handles:
        data = unreal.SubobjectDataBlueprintFunctionLibrary.get_data(handle)
        obj = unreal.SubobjectDataBlueprintFunctionLibrary.get_object(data)
        if obj is None or obj in objects:
            continue
        objects.append(obj)
        if isinstance(obj, unreal.InteractableComponent):
            interaction_count += 1
        if isinstance(obj, unreal.WidgetComponent):
            widget_class = obj.get_editor_property("widget_class")
            if widget_class and widget_class.get_path_name() == PROMPT_CLASS_PATH:
                raise RuntimeError(
                    f"World prompt remains on {asset_path}: {obj.get_path_name()}"
                )

    unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
    if interaction_count < 1:
        raise RuntimeError(f"No InteractableComponent remains on {asset_path}")
    unreal.log(
        f"[InteractionPromptVerification] {asset_path} compiled; "
        f"interactable_components={interaction_count}"
    )

unreal.log(
    f"[InteractionPromptVerification] Verified {len(CANDIDATE_BLUEPRINTS)} "
    "Blueprints with no world interaction prompt components."
)
