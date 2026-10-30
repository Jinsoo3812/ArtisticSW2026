"""Route the existing weather rain into a controlled mix; no weather logic changes."""
import json
from pathlib import Path
import shutil
import unreal


def author():
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    assert not editor or not editor.get_game_world(), "End PIE before authoring"
    root = Path(unreal.Paths.project_dir()).resolve()
    out = root / "Saved/AudioIntegration/WeatherMix"
    out.mkdir(parents=True, exist_ok=True)
    assets = unreal.EditorAssetLibrary
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    folder = "/Game/Audio/World/Mix"
    sound_class = assets.load_asset(folder + "/SC_Weather") or tools.create_asset("SC_Weather", folder, unreal.SoundClass, unreal.SoundClassFactory())
    properties = sound_class.get_editor_property("properties")
    properties.set_editor_property("volume", 0.18)
    properties.set_editor_property("default_submix", assets.load_asset("/Game/Audio/Mix/SM_Final"))
    properties.set_editor_property("apply_ambient_volumes", True)
    sound_class.set_editor_property("properties", properties)
    assert assets.save_loaded_asset(sound_class, True)
    changed = []
    paths = ["/Game/StylizedWeather/EpicContents/Rain_Heavy_Ext01", "/Game/StylizedWeather/EpicContents/Rain_Heavy_Ext01_Cue", folder + "/Mix_Underwater"]
    for package in paths:
        source = root / "Content" / (package.removeprefix("/Game/") + ".uasset")
        backup = out / "Backups" / source.relative_to(root / "Content")
        backup.parent.mkdir(parents=True, exist_ok=True)
        if not backup.exists():
            shutil.copy2(source, backup)
        asset = assets.load_asset(package)
        if isinstance(asset, unreal.SoundMix):
            adjusters = [a for a in asset.get_editor_property("sound_class_effects") if a.get_editor_property("sound_class_object") != sound_class]
            adjust = unreal.SoundClassAdjuster(sound_class_object=sound_class, volume_adjuster=0.2, low_pass_filter_frequency=800)
            adjusters.append(adjust)
            asset.set_editor_property("sound_class_effects", adjusters)
        else:
            asset.set_editor_property("sound_class_object", sound_class)
            # Weather may yield voices before nearby gameplay contacts under load.
            asset.set_editor_property("priority", 0.3)
        assert assets.save_loaded_asset(asset, True), package
        changed.append(package)
    (out / "authored.json").write_text(json.dumps({"rain_class_volume": 0.18, "assets": changed}, indent=2), encoding="utf-8")


if __name__ == "__main__":
    author()
