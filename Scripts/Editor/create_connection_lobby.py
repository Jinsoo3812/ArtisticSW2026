"""Create and verify the isolated connection lobby assets. Safe to rerun."""

import unreal

MAP_PATH = "/Game/Level/ConnectionLobby"
WIDGET_PATH = "/Game/Blueprints/02_UI/UI_Lobby/WBP_ConnectionLobby"
GAME_MODE_PATH = "/Script/ArtisticSW2026.SWConnectionLobbyGameMode"
WIDGET_CLASS_PATH = "/Script/ArtisticSW2026.SWConnectionLobbyWidget"


def make_map():
    assets = unreal.EditorAssetLibrary
    if not assets.does_asset_exist(MAP_PATH):
        if not unreal.EditorLevelLibrary.new_level(MAP_PATH):
            raise RuntimeError("Could not create lobby map")
    world = unreal.EditorLoadingAndSavingUtils.load_map(MAP_PATH)
    if not world:
        raise RuntimeError("Could not load lobby map")
    settings = world.get_world_settings()
    mode_class = unreal.load_class(None, GAME_MODE_PATH)
    if not mode_class:
        raise RuntimeError("Lobby GameMode class unavailable")
    if settings.get_editor_property("default_game_mode") != mode_class:
        settings.set_editor_property("default_game_mode", mode_class)
        if not unreal.EditorLevelLibrary.save_current_level():
            raise RuntimeError("Could not save lobby map")
    reloaded = unreal.EditorLoadingAndSavingUtils.load_map(MAP_PATH)
    if reloaded.get_world_settings().get_editor_property("default_game_mode") != mode_class:
        raise RuntimeError("Lobby GameMode override verification failed")
    unreal.log("ConnectionLobby map and GameMode override verified")


def make_widget():
    assets = unreal.EditorAssetLibrary
    parent = unreal.load_class(None, WIDGET_CLASS_PATH)
    if not parent:
        raise RuntimeError("Lobby widget native class unavailable")
    if not assets.does_asset_exist(WIDGET_PATH):
        factory = unreal.WidgetBlueprintFactory()
        factory.set_editor_property("parent_class", parent)
        package = WIDGET_PATH.rsplit("/", 1)[0]
        blueprint = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            "WBP_ConnectionLobby", package, unreal.WidgetBlueprint, factory
        )
        if not blueprint or not assets.save_asset(WIDGET_PATH):
            raise RuntimeError("Could not create or save lobby widget")
    blueprint = unreal.load_asset(WIDGET_PATH)
    generated = unreal.load_class(None, WIDGET_PATH + ".WBP_ConnectionLobby_C")
    if not blueprint or not generated or not unreal.MathLibrary.class_is_child_of(generated, parent):
        raise RuntimeError("Lobby widget parent verification failed")
    unreal.log("WBP_ConnectionLobby parent and asset path verified")


make_map()
make_widget()
