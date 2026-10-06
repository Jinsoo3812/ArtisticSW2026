import unreal

PACKAGE_PATH = "/Game/Blueprints/Room"
ASSET_NAME = "BP_SWRoomReturnPoint"
ASSET_PATH = f"{PACKAGE_PATH}/{ASSET_NAME}"
PARENT_PATH = "/Script/ClassFeature.SWRoomReturnPoint"

parent = unreal.load_class(None, PARENT_PATH)
if parent is None:
    raise RuntimeError(f"Missing C++ parent: {PARENT_PATH}")

if unreal.EditorAssetLibrary.does_asset_exist(ASSET_PATH):
    blueprint = unreal.EditorAssetLibrary.load_asset(ASSET_PATH)
else:
    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", parent)
    blueprint = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        ASSET_NAME, PACKAGE_PATH, unreal.Blueprint, factory
    )
    if blueprint is None:
        raise RuntimeError(f"Could not create {ASSET_PATH}")
    if not unreal.EditorAssetLibrary.save_loaded_asset(blueprint):
        raise RuntimeError(f"Could not save {ASSET_PATH}")

reloaded = unreal.EditorAssetLibrary.load_asset(ASSET_PATH)
if reloaded is None or not reloaded.get_path_name().startswith(ASSET_PATH + "."):
    raise RuntimeError(f"Asset verification failed: {ASSET_PATH}")
generated_class = unreal.EditorAssetLibrary.load_blueprint_class(ASSET_PATH)
if generated_class is None or not issubclass(unreal.get_type_from_class(generated_class), unreal.get_type_from_class(parent)):
    raise RuntimeError(f"Parent verification failed: {generated_class}")
print(f"VERIFIED {ASSET_PATH} parent={PARENT_PATH}")
