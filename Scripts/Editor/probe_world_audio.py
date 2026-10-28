"""Live PIE audio probe. No asset saves. Commands/results live in Saved/AudioIntegration/Live.

Run with UnrealEditor -ExecutePythonScript=<this script>; then write command.json
containing a unique id and one of inspect/begin/end/cases/record_stop/quit.
"""
import json
import time
import traceback
import os
from pathlib import Path
import unreal

unreal.EditorPythonScripting.set_keep_python_script_alive(True)
OUT = Path(unreal.Paths.project_saved_dir()).resolve() / "AudioIntegration/Live" / str(os.getpid())
OUT.mkdir(parents=True, exist_ok=True)
EDITOR = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
LEVEL = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
last_id = None
job = None
camera = None
custom_tick = None


def write(name, value):
    (OUT / name).write_text(json.dumps(value, indent=2, default=str), encoding="utf-8")


def world():
    return EDITOR.get_game_world()


def actors(cls):
    return unreal.GameplayStatics.get_all_actors_of_class(world(), cls)


def state():
    current = world()
    if not current:
        return {"world": None}
    pc = unreal.GameplayStatics.get_player_controller(current, 0)
    pawn = pc.get_controlled_pawn() if pc else None
    result = {"world": current.get_path_name(), "time": unreal.GameplayStatics.get_time_seconds(current),
              "pawn": pawn.get_path_name() if pawn else None}
    if isinstance(pawn, unreal.Character):
        movement = pawn.get_editor_property("character_movement")
        result.update(location=str(pawn.get_actor_location()), grounded=movement.is_moving_on_ground(),
                      animation=str(pawn.get_editor_property("mesh").get_anim_instance()))
    result["directors"] = [{key: str(a.get_editor_property(key)) for key in
                            ["listener_underwater", "nearest_shore_distance", "current_ocean_gain", "current_shore_gain"]}
                           for a in actors(unreal.SWOceanAudioDirector)]
    return result


def camera_at(position, look_at=None):
    global camera
    if not camera:
        library = unreal.get_default_object(unreal.GameplayStatics)
        transform = unreal.Transform()
        camera = library.call_method("BeginDeferredActorSpawnFromClass", args=(world(), unreal.CameraActor.static_class(), transform,
                                     unreal.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN, None, unreal.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
        library.call_method("FinishSpawningActor", args=(camera, transform, unreal.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
    camera.set_actor_location(unreal.Vector(*position), False, True)
    if look_at:
        camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(unreal.Vector(*position), unreal.Vector(*look_at)), False)
    unreal.GameplayStatics.get_player_controller(world(), 0).set_view_target_with_blend(camera, 0.0)


def stop_input(j):
    for key in ["move", "jump", "sprint"]:
        j["input"].inject_input_vector_for_action(j["actions"][key], unreal.Vector(), [], [])


def execute(command):
    global job, camera, custom_tick
    action = command["action"]
    if action == "inspect":
        return state()
    if action == "run_script":
        script_name = command["file"]
        assert Path(script_name).name == script_name and script_name.endswith(".py")
        script = Path(unreal.Paths.project_saved_dir()).resolve() / "AudioIntegration/LiveScripts" / script_name
        scope = {"unreal": unreal, "game_world": world(), "OUT": OUT,
                 "live_api": globals(), "result": None}
        exec(compile(script.read_text(encoding="utf-8-sig"), str(script), "exec"), scope)
        if "on_tick" in scope:
            custom_tick = scope["on_tick"]
        return scope.get("result")
    if action == "verify":
        assert not world(), "Verify saved editor map outside PIE"
        script = Path(unreal.Paths.project_dir()) / "Scripts/Editor/validate_world_audio.py"
        exec(compile(script.read_text(encoding="utf-8"), str(script), "exec"), {})
        return {"saved_map_verified": True}
    if action == "automation":
        assert not world(), "End PIE before automation"
        unreal.SystemLibrary.execute_console_command(EDITOR.get_editor_world(), "Automation RunTests SW.Audio+SW.Foley")
        return {"requested": "automation"}
    if action == "begin":
        unreal.SystemLibrary.execute_console_command(EDITOR.get_editor_world(), "SW.Audio.DebugEvents 1")
        unreal.SystemLibrary.execute_console_command(EDITOR.get_editor_world(), "SW.Foley.DebugEvents 1")
        LEVEL.editor_request_begin_play()
        return {"requested": "begin"}
    if action == "cases":
        current = world()
        pc = unreal.GameplayStatics.get_player_controller(current, 0)
        pawn = pc.get_controlled_pawn()
        assert isinstance(pawn, unreal.BasePlayer), str(pawn)
        library = unreal.get_default_object(unreal.load_class(None, "/Script/Engine.SubsystemBlueprintLibrary"))
        input_subsystem = library.call_method("GetLocalPlayerSubSystemFromPlayerController", args=(pc, unreal.EnhancedInputLocalPlayerSubsystem.static_class()))
        performance = unreal.get_default_object(unreal.load_class(None, "/Script/UnrealEd.EditorPerformanceSettings"))
        old_throttle = performance.get_editor_property("bThrottleCPUWhenNotForeground")
        performance.set_editor_property("bThrottleCPUWhenNotForeground", False)
        # Stop AI only inside this transient test world.
        for controller in actors(unreal.AIController):
            controller.stop_movement()
            brain = controller.get_editor_property("brain_component")
            if brain:
                brain.stop_logic("Bounded audio validation")
        job = {"input": input_subsystem, "pawn": pawn, "pc": pc, "world": current,
               "actions": {k: pawn.get_editor_property(k + "_action") for k in ["move", "jump", "sprint"]},
               "phases": command["phases"], "index": -1, "phase_start": 0, "samples": [],
               "performance": performance, "old_throttle": old_throttle, "label": command.get("label", "world_audio")}
        unreal.SystemLibrary.execute_console_command(current, "t.MaxFPS 60")
        unreal.SystemLibrary.execute_console_command(current, "SW.Audio.DebugEvents 1")
        unreal.SystemLibrary.execute_console_command(current, "SW.Foley.DebugEvents 1")
        unreal.AudioMixerLibrary.start_recording_output(current, 90.0, None)
        return {"started": job["label"]}
    if action == "end":
        custom_tick = None
        if job:
            stop_input(job)
            job["performance"].set_editor_property("bThrottleCPUWhenNotForeground", job["old_throttle"])
        job = None
        camera = None
        LEVEL.editor_request_end_play()
        return {"requested": "end"}
    if action == "quit":
        assert not world(), "End PIE before quitting"
        unreal.SystemLibrary.quit_editor()
        return {"requested": "quit"}
    raise ValueError(action)


def enter_phase(j, phase):
    stop_input(j)
    pawn = j["pawn"]
    if "location" in phase:
        pawn.get_editor_property("character_movement").stop_movement_immediately()
        pawn.set_actor_location(unreal.Vector(*phase["location"]), False, True)
        j["pc"].set_view_target_with_blend(pawn, 0.0)
    if "camera" in phase:
        camera_at(phase["camera"], phase.get("look_at"))
    if phase.get("restore_camera"):
        j["pc"].set_view_target_with_blend(pawn, 0.0)
    if phase.get("fire"):
        cannons = actors(unreal.Cannon)
        candidates = []
        for cannon in cannons:
            ship = cannon.get_attach_parent_actor()
            while ship and not isinstance(ship, unreal.Ship):
                ship = ship.get_attach_parent_actor()
            if not ship:
                ship = cannon.get_owner()
            if ship and (ship.actor_has_tag("Enemy") == (phase["fire"] == "enemy")) and cannon.can_fire_cannon():
                candidates.append((cannon, ship))
        candidates.sort(key=lambda c: c[0].get_distance_to(pawn))
        assert candidates, "No ready " + phase["fire"] + " cannon"
        cannon, ship = candidates[0]
        position = cannon.get_actor_location()
        camera_at([position.x - 500, position.y - 500, position.z + 250], [position.x, position.y, position.z])
        first = cannon.fire_cannon()
        second = cannon.fire_cannon()
        write(phase["name"] + "_fire.json", {"cannon": cannon.get_path_name(), "ship": ship.get_path_name() if ship else None,
                                              "accepted": first, "cooldown_attempt": second})
    unreal.log("SW_AUDIO_LIVE_PHASE " + phase["name"])


def tick(delta):
    global last_id, job, custom_tick
    try:
        path = OUT / "command.json"
        if path.exists():
            command = json.loads(path.read_text(encoding="utf-8-sig"))
            if command["id"] != last_id:
                last_id = command["id"]
                write("response.json", {"id": last_id, "result": execute(command)})
        if custom_tick:
            custom_tick(delta)
        if not job:
            return
        j = job
        now = unreal.GameplayStatics.get_time_seconds(j["world"])
        if j["index"] < 0 or now - j["phase_start"] >= j["phases"][j["index"]]["duration"]:
            j["index"] += 1
            if j["index"] >= len(j["phases"]):
                stop_input(j)
                unreal.AudioMixerLibrary.stop_recording_output(j["world"], unreal.AudioRecordingExportType.WAV_FILE, j["label"], str(OUT), None, None)
                j["performance"].set_editor_property("bThrottleCPUWhenNotForeground", j["old_throttle"])
                for cmd in ["SW.Audio.Status", "SW.Foley.Status"]:
                    unreal.SystemLibrary.execute_console_command(j["world"], cmd)
                write(j["label"] + "_samples.json", j["samples"])
                write("completed.json", {"label": j["label"], "samples": len(j["samples"]), "final": state()})
                unreal.log("SW_AUDIO_LIVE_COMPLETE " + j["label"])
                job = None
                return
            j["phase_start"] = now
            enter_phase(j, j["phases"][j["index"]])
        phase = j["phases"][j["index"]]
        j["input"].inject_input_vector_for_action(j["actions"]["move"], unreal.Vector(phase.get("move_x", 0), phase.get("move_y", 0), 0), [], [])
        jumping = bool(phase.get("jump")) and now - j["phase_start"] < 0.15
        j["input"].inject_input_vector_for_action(j["actions"]["jump"], unreal.Vector(1 if jumping else 0, 0, 0), [], [])
        if len(j["samples"]) < 10000:
            j["samples"].append({"phase": phase["name"], **state()})
    except Exception:
        write("error.json", {"id": last_id, "error": traceback.format_exc()})
        unreal.log_error(traceback.format_exc())
        if job:
            stop_input(job)
        job = None
        custom_tick = None


handle = unreal.register_slate_post_tick_callback(tick)
write("ready.json", {"editor_world": str(EDITOR.get_editor_world()), "time": time.time()})
unreal.log("SW_AUDIO_LIVE_READY")
