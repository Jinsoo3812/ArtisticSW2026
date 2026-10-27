"""Restore the exact pre-probe PIE settings after a diagnostic editor session."""
import json
from pathlib import Path
import unreal

command=unreal.SystemLibrary.get_command_line()
prefix='-AudioSettingsSnapshot='
assert prefix in command
path=Path(command.split(prefix,1)[1].split(' -',1)[0].strip().strip('"'))
snapshot=path.read_text(encoding='utf-8')
json.loads(snapshot)
unreal.SWAudioAuthoringLibrary.restore_audio_pie(snapshot)
unreal.log('SW_AUDIO_PIE_SETTINGS_RESTORED '+str(path))
