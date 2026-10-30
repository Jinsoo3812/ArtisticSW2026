import unreal,json
from pathlib import Path
w=unreal.EditorLoadingAndSavingUtils.load_map('/Game/Level/Lvl_CY')
a=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
def rec(x):
 d={'name':x.get_name(),'label':x.get_actor_label(),'class':x.get_class().get_path_name(),'folder':str(x.get_folder_path()),'location':str(x.get_actor_location()),'scale':str(x.get_actor_scale3d())}
 for p in ['enabled','spawn_mode','environment','progression_zone','progression_kind','chest_class_override','guard_characters','guard_spawners','owning_ship']:
  try:d[p]=str(x.get_editor_property(p))
  except:pass
 if 'Spawner' in d['class']:
  for p in dir(x):
   if any(s in p for s in ['spawn','zone','enemy','level','health','count','tier','difficulty','condition']):
    try:d[p]=str(x.get_editor_property(p))
    except:pass
 return d
r=[rec(x) for x in a if isinstance(x,unreal.ChestSpawnPoint) or 'Spawner' in x.get_class().get_name()]
p=Path(unreal.Paths.project_saved_dir())/'GuardedChestSetup';p.mkdir(exist_ok=True)
(p/'before.json').write_text(json.dumps(r,ensure_ascii=False,indent=2),encoding='utf-8')
unreal.log('GUARDED_CHEST_AUDIT_DONE')
