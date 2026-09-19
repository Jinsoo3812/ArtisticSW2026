import json
import unreal


DATA_TABLE_PATH = "/Game/Blueprints/Interactable/Data/DT_InteractionFeatures"
WORK_TABLE_PATHS = [
    "/Game/Blueprints/02_UI/UI_WorkTable/BP_WorkTable",
    "/Game/Blueprints/Interactable_Object/BP_WorkTable",
]
FACILITY_HUB_PATH = "/Game/Blueprints/03_WorldObject/03_FacilityHub/BP_FacilityHub"


def make_text(namespace, key, source):
    return f'NSLOCTEXT("{namespace}", "{key}", "{source}")'


def create_or_update_data_table():
    folder, asset_name = DATA_TABLE_PATH.rsplit("/", 1)
    unreal.EditorAssetLibrary.make_directory(folder)

    data_table = unreal.EditorAssetLibrary.load_asset(DATA_TABLE_PATH)
    if data_table is None:
        factory = unreal.DataTableFactory()
        factory.set_editor_property("struct", unreal.InteractionFeatureData.static_struct())
        data_table = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            asset_name,
            folder,
            unreal.DataTable,
            factory,
        )
    if data_table is None:
        raise RuntimeError(f"Failed to create {DATA_TABLE_PATH}")

    rows = {
        "Interactable.Id.WorkTable": ("WorkTable", "Use"),
        "Interactable.Id.FacilityHub": ("Facility Hub", "Use"),
        "Interactable.Id.StorageChest": ("Storage Chest", "Open"),
        "Interactable.Id.NPC": ("NPC", "Talk"),
        "Interactable.Id.Ship.Helm": ("Ship Helm", "Take Control"),
        "Interactable.Id.Ship.Cannon": ("Cannon", "Use"),
        "Interactable.Id.Ship.BoardingPoint": ("Ship", "Board"),
        "Interactable.Id.Ship.Anchor": ("Anchor", "Drop Anchor"),
        "Interactable.Id.Ship.RepairPoint": ("Hull Leak", "Hold F to repair"),
    }
    payload = []
    for row_name, (object_name, action_text) in rows.items():
        suffix = row_name.replace("Interactable.Id.", "").replace(".", "_")
        payload.append(
            {
                "Name": row_name,
                "ObjectName": make_text(
                    "InteractionFeatureDataTable",
                    f"{suffix}_ObjectName",
                    object_name,
                ),
                "ActionText": make_text(
                    "InteractionFeatureDataTable",
                    f"{suffix}_ActionText",
                    action_text,
                ),
            }
        )

    if not unreal.DataTableFunctionLibrary.fill_data_table_from_json_string(
        data_table,
        json.dumps(payload),
    ):
        raise RuntimeError(f"Failed to populate {DATA_TABLE_PATH}")

    unreal.EditorAssetLibrary.save_loaded_asset(data_table, only_if_is_dirty=False)
    unreal.log(f"[InteractionAuthoring] Wrote {len(payload)} rows to {DATA_TABLE_PATH}")


def connect_interactable(asset_path, tag_name):
    blueprint = unreal.EditorAssetLibrary.load_asset(asset_path)
    if blueprint is None:
        raise RuntimeError(f"Failed to load {asset_path}")

    subsystem = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    handles = subsystem.k2_gather_subobject_data_for_blueprint(blueprint)
    components = []
    for handle in handles:
        data = unreal.SubobjectDataBlueprintFunctionLibrary.get_data(handle)
        obj = unreal.SubobjectDataBlueprintFunctionLibrary.get_object(data)
        if isinstance(obj, unreal.InteractableComponent):
            if obj not in components:
                components.append(obj)
    if len(components) != 1:
        details = ", ".join(
            f"{component.get_name()} ({component.get_path_name()})"
            for component in components
        )
        raise RuntimeError(
            f"Expected exactly one InteractableComponent on {asset_path}; "
            f"found {len(components)}. Candidates: {details}"
        )
    component = components[0]

    interactable_id = unreal.GameplayTag()
    interactable_id.import_text(f'(TagName="{tag_name}")')
    component.set_editor_property("interactable_id_tag", interactable_id)
    unreal.EditorAssetLibrary.save_loaded_asset(blueprint, only_if_is_dirty=False)

    saved_tag = component.get_editor_property("interactable_id_tag")
    saved_text = saved_tag.export_text()
    if tag_name not in saved_text:
        raise RuntimeError(
            f"Failed to save {tag_name} on {asset_path}: {saved_text}"
        )
    unreal.log(
        f"[InteractionAuthoring] Connected {asset_path}.{component.get_name()} "
        f"to {tag_name}"
    )


if __name__ == "__main__":
    create_or_update_data_table()
    for path in WORK_TABLE_PATHS:
        connect_interactable(path, "Interactable.Id.WorkTable")
    connect_interactable(FACILITY_HUB_PATH, "Interactable.Id.FacilityHub")
