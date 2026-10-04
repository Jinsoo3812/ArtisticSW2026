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
    handle = unreal.register_slate_post_tick_callback(poll)
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_begin_play()
except Exception:
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)
    raise
