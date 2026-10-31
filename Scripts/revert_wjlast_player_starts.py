import unreal,json,re
ROOT='C:/Users/wonkii/Documents/GitHub/ArtisticSW2026'
s=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
assert w.get_path_name()=='/Game/Level/Lvl_WJLast.Lvl_WJLast'
actors=list(s.get_all_level_actors())
r={'removed_starts':[],'restored_entry_locations':[],'preserved_user_entries':[],'player_ships':[]}
original={'BP_LevelEntryPoint_Host':unreal.Vector(215370,-124160,430),'BP_LevelEntryPoint_Guest':unreal.Vector(215370.000004,-123920,430),'BP_LevelEntryPoint_Ship':unreal.Vector(211767.250070,-117973.215552,1710)}
last_ours={'BP_LevelEntryPoint_Host':unreal.Vector(261025.488295,-168732.957166,835.149120),'BP_LevelEntryPoint_Guest':unreal.Vector(261025.488295,-169152.957166,835.149120),'BP_LevelEntryPoint_Ship':unreal.Vector(261165.488295,-168942.957166,-44.850880)}
with unreal.ScopedEditorTransaction('Remove assistant-authored player start placement'):
 for a in actors:
  label=a.get_actor_label()
  if label in ['WJLast_PlayerStart_Host','WJLast_PlayerStart_Guest']:
   r['removed_starts'].append({'label':label,'location':str(a.get_actor_location())})
   assert s.destroy_actor(a)
  elif label in original:
   loc=a.get_actor_location(); ours=last_ours[label]
   if (loc-ours).length()<1:
    a.set_actor_location(original[label],False,True)
    r['restored_entry_locations'].append(label)
   else:r['preserved_user_entries'].append({'label':label,'location':str(loc)})
  elif label=='BP_PlayerShip_Kelvin':
   r['player_ships'].append({'name':label,'location':str(a.get_actor_location()),'rotation':str(a.get_actor_rotation()),'hidden':a.get_editor_property('hidden')})
assert len(r['player_ships'])==1
assert not any(a.get_actor_label().startswith('WJLast_PlayerStart_') for a in s.get_all_level_actors())
assert unreal.EditorLoadingAndSavingUtils.save_map(w,'/Game/Level/Lvl_WJLast')
open(ROOT+'/Saved/WJLast_player_start_revert.json','w').write(json.dumps(r,indent=2))
print('WJLAST_START_REVERT_DONE')
