"""Load/render the actual editor map without PIE, gameplay mutation or asset saves."""
import json,time
from pathlib import Path
import unreal
ROOT=Path(unreal.Paths.project_dir()).resolve();OUT=ROOT/'Saved/AssetRepair'
editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
world=editor.get_editor_world();assert world and '/Game/Level/Lvl_CY' in world.get_path_name()
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
started=time.monotonic()
def tick(delta):
    if time.monotonic()-started<5:return
    result={'passed':True,'world':world.get_path_name(),'loaded_actors':len(unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()),'scope':'editor load/render only; no PIE or asset saves'}
    (OUT/'world_smoke.json').write_text(json.dumps(result,indent=2),encoding='utf8')
    unreal.unregister_slate_post_tick_callback(handle);unreal.EditorPythonScripting.set_keep_python_script_alive(False);unreal.SystemLibrary.quit_editor()
handle=unreal.register_slate_post_tick_callback(tick)
