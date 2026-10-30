"""Configure the two user-placed guarded chests without changing enemy spawners."""
import json, shutil
from pathlib import Path
import unreal
OUT=Path(unreal.Paths.project_saved_dir())/'GuardedChestSetup'
OUT.mkdir(exist_ok=True)
VERIFY='-GuardedVerify' in unreal.SystemLibrary.get_command_line()
if not VERIFY:
 backup=OUT/'Lvl_CY.before_guarded_setup.umap'
 if not backup.exists(): shutil.copy2(Path(unreal.Paths.project_content_dir())/'Level/Lvl_CY.umap',backup)
w=unreal.EditorLoadingAndSavingUtils.load_map('/Game/Level/Lvl_CY')
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
def one(label):
 matches=[a for a in actors if a.get_actor_label()==label]
 assert len(matches)==1,(label,len(matches))
 return matches[0]
# Use exactly the normal-sized class used by the majority of existing chests.
normal=unreal.load_class(None,'/Game/Blueprints/Item/BP/Chest/BP_Storage_Chest.BP_Storage_Chest_C')
assert normal
rows=[]
for label,sl,zone,tier,count in [('BP_ChestSpawnPoint_Land_Guarded','BP_ESpawner_Z1',unreal.ProgressionZone.MID1,'T1',3),('BP_ChestSpawnPoint_Land_Guarded2','BP_ESpawner_Z2',unreal.ProgressionZone.MID2,'T2',5)]:
 p,s=one(label),one(sl)
 entries=list(s.get_editor_property('spawn_entries'))
 assert sum(e.get_editor_property('count') for e in entries)==count
 assert s.validate_configuration()
 assert not p.call_method("IsBossChest")
 tags=[e.get_editor_property('enemy_type_tag').export_text() for e in entries]
 assert all(tier in tag and 'Boss' not in tag for tag in tags),tags
 settings={'enabled':True,'spawn_mode':unreal.ChestSpawnMode.GUARDED,'environment':unreal.ChestEnvironment.LAND,'progression_zone':zone,'progression_kind':unreal.ProgressionChestKind.ISLAND_GUARDED,'chest_class_override':normal,'guard_characters':[],'guard_spawners':[],'owning_ship':None,'align_chest_bottom_to_ground':True}
 if not VERIFY:
  for k,v in settings.items():p.set_editor_property(k,v)
 components=list(p.get_components_by_class(unreal.ChestGroundGuardBindingComponent))
 if not components and not VERIFY:
  sub=unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
  lib=unreal.SubobjectDataBlueprintFunctionLibrary
  handles=sub.k2_gather_subobject_data_for_instance(p)
  root=next(h for h in handles if lib.get_associated_object(lib.get_data(h))==p)
  handle,failure=sub.add_new_subobject(unreal.AddNewSubobjectParams(parent_handle=root,new_class=unreal.ChestGroundGuardBindingComponent))
  assert not str(failure),str(failure)
  components=list(p.get_components_by_class(unreal.ChestGroundGuardBindingComponent))
 assert len(components)==1,(label,len(components))
 if not VERIFY:components[0].set_editor_property('guard_spawner',s)
 for k,v in settings.items():
  actual=p.get_editor_property(k)
  assert list(actual)==v if isinstance(v,list) else actual==v,(label,k,str(actual))
 assert components[0].get_editor_property('guard_spawner')==s
 scale=p.get_actor_scale3d()
 rows.append({'chest':label,'spawner':sl,'zone':str(zone),'kind':str(p.get_progression_kind()),'class':normal.get_path_name(),'scale':[scale.x,scale.y,scale.z],'boss':p.call_method("IsBossChest"),'entries':[e.export_text() for e in entries],'tags':tags,'location':p.get_actor_location().export_text()})
if not VERIFY:assert unreal.EditorLoadingAndSavingUtils.save_map(w,'/Game/Level/Lvl_CY')
(OUT/('verified.json' if VERIFY else 'applied.json')).write_text(json.dumps(rows,ensure_ascii=False,indent=2),encoding='utf-8')
unreal.log('GUARDED_CHEST_'+('VERIFIED' if VERIFY else 'SAVED'))



