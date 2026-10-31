import json
w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
r={'world':str(w),'ships':[],'players':[]}
if w:
 for a in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.Actor):
  if isinstance(a,unreal.EnemyShip):
   c=a.get_component_by_class(unreal.BossEncounterComponent)
   r['ships'].append({'name':a.get_name(),'loc':str(a.get_actor_location()),'state':str(c.get_encounter_state()),'boss':str(c.get_spawned_boss())})
  elif isinstance(a,unreal.Pawn) and 'Player' in a.get_class().get_name():r['players'].append({'name':a.get_name(),'class':a.get_class().get_name(),'loc':str(a.get_actor_location())})
open('C:/Users/wonkii/Documents/GitHub/ArtisticSW2026/Saved/WJLast_play_audit.json','w').write(json.dumps(r,indent=2))

