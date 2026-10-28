import json
snapshot = unreal.SWAudioAuthoringLibrary.configure_audio_pie(2)
(OUT/'pie_settings_original.json').write_text(snapshot,encoding='utf-8')
settings = unreal.get_default_object(unreal.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings'))
throttle = settings.get_editor_property('bThrottleCPUWhenNotForeground')
(OUT/'performance_original.json').write_text(json.dumps({'throttle':throttle,'max_fps':unreal.SystemLibrary.get_console_variable_float_value('t.MaxFPS')}))
settings.set_editor_property('bThrottleCPUWhenNotForeground',False)
unreal.SystemLibrary.execute_console_command(live_api['EDITOR'].get_editor_world(),'t.MaxFPS 60')
result = {'settings':json.loads(snapshot),'configured_clients':2}
