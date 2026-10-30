"""UE 5.8: export a small set of ship/water candidates; never save source assets."""
import hashlib
import json
from pathlib import Path
import unreal

command = unreal.SystemLibrary.get_command_line()
prefix = "-AudioExportDir="
assert prefix in command
out = Path(command.split(prefix, 1)[1].split(" -", 1)[0].strip().strip('"')).resolve()
out.mkdir(parents=True, exist_ok=True)
groups = {
    "Nature_Sound": ["Loop_River_Ambience__Boat1", "Loop_Running_Water1", "Loop_Running_Water2"],
    "Movement_Sound": ["Loop_Paddling1", "Loop_Paddling2_Slow", "Oneshot_Diving1", "Oneshot_Diving2", "Oneshot_Diving3"] +
                      ["Oneshot_Splash" + str(i) for i in range(1, 7)],
}
report = []
if '-AudioExportExtra' in command:
    groups = {
        'Nature_Sound': ['Oneshot_Rushing_Wave3_Short', 'Oneshot_Rushing_Wave5_Short', 'Oneshot_Wave_Crash2', 'Oneshot_Wave_Crash4', 'Oneshot_Wave_Crash5'],
        'Movement_Sound': ['Oneshot_Swimming_Heavy1', 'Oneshot_Swimming_Heavy2', 'Oneshot_Swimming_Heavy3', 'Oneshot_Diving_Underwater1'],
    }
for folder, names in groups.items():
    for name in names:
        package = "/Game/WaterSFX/WAV/" + folder + "/" + name
        source = Path(unreal.Paths.project_content_dir()).resolve() / (package.removeprefix("/Game/") + ".uasset")
        before = hashlib.sha256(source.read_bytes()).hexdigest()
        sound = unreal.load_asset(package)
        assert isinstance(sound, unreal.SoundWave), package
        task = unreal.AssetExportTask()
        task.object = sound
        task.filename = str(out / (name + ".wav"))
        task.automated = True
        task.prompt = False
        task.replace_identical = True
        task.exporter = unreal.SoundExporterWAV()
        assert unreal.Exporter.run_asset_export_task(task), package
        assert before == hashlib.sha256(source.read_bytes()).hexdigest(), "Source asset changed"
        report.append({"source": sound.get_path_name(), "file": name + ".wav", "source_sha256": before,
                       "bytes": Path(task.filename).stat().st_size})
(out / "export_manifest.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
unreal.log("SW_SHIP_WATER_EXPORT_COMPLETE count=" + str(len(report)))
