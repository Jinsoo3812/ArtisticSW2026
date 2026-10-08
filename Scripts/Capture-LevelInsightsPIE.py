"""Run from -ExecutePythonScript with SWInsightsCapture/Seconds/Warmup flags.

Keeps this dedicated editor alive until the bounded C++ PIE trace finishes.
Does not save the map or change its assets.
"""
import json
import re
import time
from pathlib import Path

import unreal

command_line = unreal.SystemLibrary.get_command_line()
label_match = re.search(r"SWInsightsCapture=([^\s\"]+)", command_line)
if not label_match:
    raise RuntimeError("Pass -SWInsightsCapture=<label> to enable a bounded capture")
label = label_match.group(1)
map_match = re.search(r"SWInsightsMap=([^\s\"]+)", command_line)
map_path = map_match.group(1) if map_match else "/Game/Level/Lvl_CY"
warmup_match = re.search(r"SWInsightsWarmup=(\d+)", command_line)
seconds_match = re.search(r"SWInsightsSeconds=(\d+)", command_line)
timeout_seconds = (int(warmup_match.group(1)) if warmup_match else 10) + (int(seconds_match.group(1)) if seconds_match else 20) + 120
output = Path(unreal.Paths.project_saved_dir()) / "Profiling" / "Insights"
started = time.monotonic()
last_poll = 0.0
handle = None
controlled = '-SWControlledComparison' in command_line
hour_match = re.search(r'SWComparisonHour=(\d+)', command_line)
comparison_hour = int(hour_match.group(1)) if hour_match else 18
if controlled:
    timeout_seconds += 1800  # Startup shader permutations can take several minutes.


def finish(message):
    unreal.log(message)
    unreal.unregister_slate_post_tick_callback(handle)
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)


def poll(delta_seconds):
    global last_poll
    now = time.monotonic()
    if now - last_poll < 1.0:
        return
    last_poll = now
    for metadata_path in output.glob(label + "_*.json"):
        if metadata_path.stat().st_mtime < startup_timestamp:
            continue
        try:
            metadata = json.loads(metadata_path.read_text(encoding="utf-8-sig"))
        except (OSError, ValueError):
            continue
        if "stopped_own_trace" in metadata:
            finish("SW_PIE_CAPTURE_COMPLETE: " + str(metadata_path))
            return
    # Validation timeout only: normal captures complete through the C++ wall-clock timer.
    if now - started > timeout_seconds:
        finish("SW_PIE_CAPTURE_TIMEOUT: inspect the editor log")


startup_timestamp = time.time()
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
try:
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    if not world or world.get_path_name().split(".")[0] != map_path:
        world = unreal.EditorLoadingAndSavingUtils.load_map(map_path)
    if not world:
        raise RuntimeError("Could not load " + map_path)
    if controlled:
        unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_set_viewport_realtime(False)
        # The native network weather actor reads these at BeginPlay. A zero hour
        # duration selects its supported Frozen state; pausing BP timelines alone
        # does not stop native playback. This dedicated editor never saves assets.
        weather = [actor for actor in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Actor)
                   if 'BP_StylizedWeather' in actor.get_class().get_name()]
        if len(weather) != 1:
            raise RuntimeError('Controlled comparison requires exactly one weather actor')
        weather[0].set_editor_property('1 Hour Seconds', 0.0)
        weather[0].set_editor_property('Init Hour', comparison_hour)
        weather[0].set_editor_property('Init Minute', 0)
        unreal.log(f'SW_COMPARISON_WEATHER_FROZEN: {comparison_hour}:00, native zero-duration playback')
    handle = unreal.register_slate_post_tick_callback(poll)
    started = time.monotonic()
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_begin_play()
except Exception:
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)
    raise
