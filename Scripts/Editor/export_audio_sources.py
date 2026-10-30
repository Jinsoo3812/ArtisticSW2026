"""Run in the source UE project. Export selected SoundWaves only; never save source assets.

UnrealEditor-Cmd <source.uproject> -run=pythonscript -script=<this file>
  -AudioExportDir=<absolute workspace staging directory> -unattended -nullrhi -nosound
"""
import json
from pathlib import Path
import unreal


def argument(name):
    prefix = "-" + name + "="
    for item in unreal.SystemLibrary.get_command_line().split(' -'):
        item = item if item.startswith('-') else '-' + item
        if item.startswith(prefix):
            return item[len(prefix):].strip('"')
    raise RuntimeError("Missing " + prefix)


out = Path(argument("AudioExportDir")).resolve()
out.mkdir(parents=True, exist_ok=True)
registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.search_all_assets(True)
selected = []
for surface in ["SAND", "SOIL", "GRASS", "WATER", "WOOD", "ROCKS", "METAL_1"]:
    root = "/Game/Footstep_Essentials/Wavs/" + surface
    for action in ["WALK", "RUN", "JUMP_LAND"]:
        assets = sorted(registry.get_assets_by_path(root + "/" + action, recursive=True),
                        key=lambda a: str(a.asset_name))
        if action == "JUMP_LAND":
            assets = [a for a in assets if "JUMP" in str(a.asset_name)][:3] + [
                a for a in assets if "LAND" in str(a.asset_name)][:3]
        else:
            assets = assets[:6]
        for asset in assets:
            selected.append((asset, "Footsteps/" + surface + "/" + action))

water_names = {
    "Loop_Underwater1", "Loop_Underwater3", "Loop_Sea_Shore_Ambience1",
    "Loop_Ocean_Wave5", "Loop_Ocean_Wave7", "Oneshot_Wave_Crash1",
    "Oneshot_Wave_Crash3", "Oneshot_Splash1", "Oneshot_Splash2", "Oneshot_Splash4",
    "Oneshot_Splash6", "Oneshot_Diving1", "Oneshot_Diving2",
    "Oneshot_Swimming_Stroke1", "Oneshot_Swimming_Stroke2",
    "Oneshot_Underwater_swimming1", "Oneshot_Underwater_swimming2",
    "Oneshot_Waterdrop1", "Oneshot_Waterdrop2", "Oneshot_Seagull1", "Oneshot_Seagull2",
}
for asset in registry.get_assets_by_path("/Game/WaterSFX/WAV", recursive=True):
    if str(asset.asset_name) in water_names:
        selected.append((asset, "Water"))

report = []
for data, relative in selected:
    sound = data.get_asset()
    if not isinstance(sound, unreal.SoundWave):
        continue
    dest = out / relative / (str(data.asset_name) + ".wav")
    dest.parent.mkdir(parents=True, exist_ok=True)
    task = unreal.AssetExportTask()
    task.object = sound
    task.filename = str(dest)
    task.automated = True
    task.prompt = False
    task.replace_identical = True
    task.exporter = unreal.SoundExporterWAV()
    if not unreal.Exporter.run_asset_export_task(task) or not dest.exists():
        raise RuntimeError("SoundWave export failed: " + sound.get_path_name())
    report.append({"source": sound.get_path_name(), "file": str(dest.relative_to(out)),
                   "bytes": dest.stat().st_size})
with (out / "export_manifest.json").open("w", encoding="utf-8") as f:
    json.dump(report, f, indent=2)
unreal.log("SW_AUDIO_EXPORT_COMPLETE count=" + str(len(report)))
