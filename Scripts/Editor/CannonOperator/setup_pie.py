import json
snapshot=unreal.SWAudioAuthoringLibrary.configure_audio_pie(2)
if not (OUT/'pie_original.json').exists():(OUT/'pie_original.json').write_text(snapshot)
settings=unreal.get_default_object(unreal.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings'))
if not (OUT/'performance_original.json').exists():(OUT/'performance_original.json').write_text(json.dumps({'throttle':settings.get_editor_property('bThrottleCPUWhenNotForeground'),'max_fps':unreal.SystemLibrary.get_console_variable_float_value('t.MaxFPS')}))
settings.set_editor_property('bThrottleCPUWhenNotForeground',False)
unreal.SystemLibrary.execute_console_command(live_api['EDITOR'].get_editor_world(),'t.MaxFPS 30')
live_api['LEVEL'].editor_request_begin_play()
result={'requested':'two-player PIE','settings':json.loads(snapshot)}
