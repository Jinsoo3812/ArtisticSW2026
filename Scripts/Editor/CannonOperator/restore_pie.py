import json
assert not unreal.SWAudioAuthoringLibrary.get_audio_pie_worlds()
unreal.SWAudioAuthoringLibrary.restore_audio_pie((OUT/'pie_original.json').read_text())
old=json.loads((OUT/'performance_original.json').read_text())
settings=unreal.get_default_object(unreal.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings'))
settings.set_editor_property('bThrottleCPUWhenNotForeground',old['throttle'])
unreal.SystemLibrary.execute_console_command(live_api['EDITOR'].get_editor_world(),'t.MaxFPS '+str(old['max_fps']))
result={'restored':True}
(OUT/'manual_zoom_restored.json').write_text(json.dumps(result))

if (OUT/'cannon_socket_adjustment.json').exists():
    mesh=unreal.load_asset('/Game/Blueprints/Ship/Cannon/Mesh/Operator/SM_OperatorBarrelVisual')
    assert unreal.EditorAssetLibrary.save_loaded_asset(mesh,False)
