"""Actual input-driven surface swimming on both network peers; save no PIE assets."""
import json
OUT = OUT / 'SwimContactsVerified'; OUT.mkdir(exist_ok=True)
worlds = unreal.SWAudioAuthoringLibrary.get_audio_pie_worlds()
assert len(worlds) == 2
server = next(w for w in worlds if 'NetMode=2 ' in unreal.SWAudioAuthoringLibrary.describe_audio_world(w))
worlds = [server, next(w for w in worlds if w != server)]
allof = unreal.GameplayStatics.get_all_actors_of_class
lib = unreal.get_default_object(unreal.GameplayStatics)
subsystems = unreal.get_default_object(unreal.load_class(None, '/Script/Engine.SubsystemBlueprintLibrary'))
pcs = [unreal.GameplayStatics.get_player_controller(w, 0) for w in worlds]
owners = [pc.get_controlled_pawn() for pc in pcs]
names = [p.get_name() for p in owners]
assert len(set(names)) == 2 and all(isinstance(p, unreal.BasePlayer) for p in owners)
inputs = [subsystems.call_method('GetLocalPlayerSubSystemFromPlayerController', args=(pc, unreal.EnhancedInputLocalPlayerSubsystem.static_class())) for pc in pcs]
actions = [p.get_editor_property('move_action') for p in owners]
ship = next(s for s in allof(server, unreal.Ship) if not isinstance(s, unreal.EnemyShip) and not s.get_editor_property('hidden'))
origin = ship.get_actor_location()
cameras = []
for w, pc in zip(worlds, pcs):
    transform = unreal.Transform(location=origin + unreal.Vector(-2500, 5500, 500))
    cam = lib.call_method('BeginDeferredActorSpawnFromClass', args=(w, unreal.CameraActor.static_class(), transform, unreal.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN, None, unreal.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
    lib.call_method('FinishSpawningActor', args=(cam, transform, unreal.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
    pc.set_view_target_with_blend(cam, 0.)
    pc.set_audio_listener_attenuation_override(cam.get_editor_property('root_component'), unreal.Vector())
    pc.set_control_rotation(unreal.Rotator())
    cameras.append(cam)
    for a in allof(w, unreal.Actor):
        if isinstance(a, unreal.SWOceanAudioDirector): a.set_editor_property('bEnabled', False)
        if isinstance(a, unreal.SWNetworkWeatherActor) or 'WeatherEvent_Effect' in a.get_class().get_name():
            a.set_actor_tick_enabled(False)
            for timeline in a.get_components_by_class(unreal.TimelineComponent): timeline.stop()
        for audio in a.get_components_by_class(unreal.AudioComponent): audio.stop()
for i, name in enumerate(names):
    pawn = next(p for p in allof(server, unreal.BasePlayer) if p.get_name() == name)
    pawn.set_actor_location(origin + unreal.Vector(-2500, 5500 + i * 600, 1200), False, True)
    pawn.get_editor_property('character_movement').set_movement_mode(unreal.MovementMode.MOVE_FALLING, 0)
phases = [('settle', 10), ('idle', 6), ('host_forward', 8), ('host_stop', 5), ('client_forward', 8), ('client_stop', 5), ('host_left', 6), ('host_back', 6), ('final_stop', 5), ('host_dive', 4), ('underwater_forward', 7), ('underwater_stop', 5)]
phase = -1; began = 0.; closing = 0.; second_stopped = False; done = False; last_sample = 0.; last_quiet = -10.; rows = []

def stop(i):
    unreal.AudioMixerLibrary.stop_recording_output(worlds[i], unreal.AudioRecordingExportType.WAV_FILE,
        'network_verified_swim_' + phases[phase][0] + '_device' + str(i), str(OUT), None, None)

def on_tick(delta):
    global phase, began, closing, second_stopped, done, last_sample, last_quiet
    if done: return
    now = unreal.GameplayStatics.get_time_seconds(server)
    advance = phase < 0
    if phase >= 0 and not closing and now - began >= phases[phase][1]: stop(0); closing = now; second_stopped = False
    if closing and not second_stopped and now - closing >= .6: stop(1); second_stopped = True
    if closing and now - closing >= 1.2: advance = True; closing = 0.
    if advance:
        phase += 1
        if phase >= len(phases):
            for inp, action in zip(inputs, actions): inp.inject_input_vector_for_action(action, unreal.Vector(), [], [])
            (OUT / 'samples.json').write_text(json.dumps(rows, indent=2))
            (OUT / 'complete.json').write_text(json.dumps({'owners': names, 'phases': phases, 'samples': len(rows)}, indent=2))
            done = True; unreal.log('SW_SWIM_NETWORK_COMPLETE'); return
        began = now; unreal.log('SW_SWIM_NETWORK_PHASE ' + phases[phase][0])
        if phases[phase][0] == 'host_dive': owners[0].start_swim_dive()
        else: owners[0].stop_swim_dive()
        for w in worlds: unreal.AudioMixerLibrary.start_recording_output(w, 25, None)
    label = phases[phase][0]
    active = 1 if label == 'client_forward' else 0
    moving = label in ['host_forward', 'client_forward', 'host_left', 'host_back', 'underwater_forward']
    vector = unreal.Vector(-1, 0, 0) if label == 'host_left' else unreal.Vector(0, -1, 0) if label == 'host_back' else unreal.Vector(0, 1, 0)
    for i, (inp, action) in enumerate(zip(inputs, actions)):
        inp.inject_input_vector_for_action(action, vector if moving and active == i else unreal.Vector(), [], [])
    if now - last_quiet > 1.:
        last_quiet = now
        for w in worlds:
            for s in allof(w, unreal.Ship): s.set_actor_hidden_in_game(True)
            for enemy in allof(w, unreal.BaseEnemy):
                enemy.get_editor_property('character_movement').disable_movement()
                foley = enemy.get_component_by_class(unreal.SWFoleyComponent)
                if foley: foley.set_editor_property('bEnabled', False)
                ai = enemy.get_controller()
                if isinstance(ai, unreal.AIController):
                    ai.stop_movement(); brain = ai.get_editor_property('brain_component')
                    if brain: brain.stop_logic('Isolated swim audio verification')
    sample = {'phase': label, 'time': now, 'elapsed': now - began, 'worlds': []}
    for w, cam in zip(worlds, cameras):
        pawns = [p for p in allof(w, unreal.BasePlayer) if p.get_name() in names]
        focus = next(p for p in pawns if p.get_name() == names[active])
        pos = focus.get_actor_location()
        cam_pos = pos + unreal.Vector(-280, -380, 240)
        cam.set_actor_location(cam_pos, False, True)
        cam.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(cam_pos, pos + unreal.Vector(0, 0, 20)), False)
        characters = []
        for p in pawns:
            mesh = p.get_editor_property('mesh')
            swim = p.get_component_by_class(unreal.SwimmingComponent)
            characters.append({'name': p.get_name(), 'swimming': swim.is_custom_swimming(), 'underwater': swim.is_underwater(),
                'location': str(p.get_actor_location()), 'velocity': str(p.get_velocity()),
                'left_hand': str(mesh.get_socket_location('hand_l')), 'right_hand': str(mesh.get_socket_location('hand_r')),
                'authored_surface': p.get_component_by_class(unreal.SWFoleyComponent).get_editor_property('bUseAnimationDrivenSurfaceStrokes')})
        sample['worlds'].append({'description': unreal.SWAudioAuthoringLibrary.describe_audio_world(w), 'characters': characters})
    if now - last_sample >= .05: last_sample = now; rows.append(sample)

result = {'owners': names, 'phases': phases}
