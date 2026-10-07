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

hud_class = unreal.EditorAssetLibrary.load_blueprint_class(
    "/Game/Blueprints/02_UI/UI_HUD/WBP_PlayerHUD"
)
if hud_class is None:
    raise RuntimeError("Failed to load WBP_PlayerHUD class")
hud_default = unreal.get_default_object(hud_class)
prompt_class = hud_default.get_editor_property("interaction_prompt_widget_class")
if prompt_class is None or prompt_class.get_path_name() != PROMPT_CLASS_PATH:
    raise RuntimeError(f"Unexpected HUD prompt class: {prompt_class}")

data_table = unreal.EditorAssetLibrary.load_asset(
    "/Game/Blueprints/Interactable/Data/DT_InteractionFeatures"
)
row_names = unreal.DataTableFunctionLibrary.get_data_table_row_names(data_table)
if len(row_names) != 9:
    raise RuntimeError(f"Expected 9 interaction rows, found {len(row_names)}")

unreal.log(
    f"[InteractionPromptVerification] HUD prompt class={prompt_class.get_path_name()} "
    f"interaction_rows={len(row_names)}"
)

facility_path = "/Game/Blueprints/03_WorldObject/03_FacilityHub/BP_FacilityHub"
facility_blueprint = unreal.EditorAssetLibrary.load_asset(facility_path)
facility_handles = subsystem.k2_gather_subobject_data_for_blueprint(facility_blueprint)
facility_components = []
for handle in facility_handles:
    data = unreal.SubobjectDataBlueprintFunctionLibrary.get_data(handle)
    obj = unreal.SubobjectDataBlueprintFunctionLibrary.get_object(data)
    if isinstance(obj, unreal.InteractableComponent) and obj not in facility_components:
        facility_components.append(obj)
if len(facility_components) != 1:
    raise RuntimeError(
        f"Expected one FacilityHub interactable, found {len(facility_components)}"
    )
facility_tag = facility_components[0].get_editor_property(
    "interactable_id_tag"
).export_text()
if "Interactable.Id.FacilityHub" not in facility_tag:
    raise RuntimeError(f"Unexpected FacilityHub ID: {facility_tag}")
unreal.BlueprintEditorLibrary.compile_blueprint(facility_blueprint)
unreal.log(
    f"[InteractionPromptVerification] FacilityHub ID={facility_tag} compiled"
)
