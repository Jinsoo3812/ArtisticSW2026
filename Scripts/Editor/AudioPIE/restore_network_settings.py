import json
if live_api.pop('all_pie_audio_toggled',False):
 unreal.SystemLibrary.execute_console_command(live_api['EDITOR'].get_editor_world(),'au.Debug.PlayAllPIEAudio')
unreal.SWAudioAuthoringLibrary.restore_audio_pie((OUT/'pie_settings_original.json').read_text(encoding='utf-8'))
settings=unreal.get_default_object(unreal.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings'))
settings.set_editor_property('bThrottleCPUWhenNotForeground',json.loads((OUT/'performance_original.json').read_text())['throttle'])
live_api.pop('network_fixture',None)
unreal.SystemLibrary.execute_console_command(live_api['EDITOR'].get_editor_world(),'t.MaxFPS '+str(json.loads((OUT/'performance_original.json').read_text()).get('max_fps',0.)))
result={'restored':True}
