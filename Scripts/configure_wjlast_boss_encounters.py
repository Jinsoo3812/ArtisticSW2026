import unreal, json, os, shutil
ROOT='C:/Users/wonkii/Documents/GitHub/ArtisticSW2026'
w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
assert w.get_path_name()=='/Game/Level/Lvl_WJLast.Lvl_WJLast'
s=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
actors=list(s.get_all_level_actors())
keep=['BP_ES_Mid_1','BP_ES_Mid_3','BP_ES_Final']
ships={a.get_actor_label():a for a in actors if isinstance(a,unreal.EnemyShip)}
assert all(n in ships for n in keep)
p=next(a for a in actors if a.get_actor_label()=='BP_PlayerShip_Kelvin')
report={'removed':[],'ships':[],'debug_assets':[]}
# Turn off saved Blueprint overrides as well as inherited defaults.
paths=[str(x).split('.')[0] for x in unreal.EditorAssetLibrary.list_assets('/Game/GameplayAbilitySystem/Enemy',recursive=True) if 'DeckRangedEnemy.' in str(x)]
for path in paths:
 bp=unreal.load_asset(path)
 if not isinstance(bp,unreal.Blueprint):continue
 rel=path.replace('/Game/','Content/')+'.uasset'
 src=ROOT+'/'+rel; dst=ROOT+'/Saved/WJLast_before_edit/'+rel
 os.makedirs(os.path.dirname(dst),exist_ok=True)
 if os.path.isfile(src) and not os.path.exists(dst):shutil.copy2(src,dst)
 cdo=unreal.get_default_object(bp.generated_class())
 changes=[]
 if cdo.get_editor_property('draw_attack_line_of_sight'):
  cdo.set_editor_property('draw_attack_line_of_sight',False); changes.append('draw_attack_line_of_sight')
 for c in cdo.get_components_by_class(unreal.ActorComponent):
  for prop in ['draw_tracking_debug','draw_collision_debug']:
   try:
    if c.get_editor_property(prop):c.set_editor_property(prop,False);changes.append(c.get_name()+'.'+prop)
   except:pass
 if changes:unreal.EditorAssetLibrary.save_loaded_asset(bp,only_if_is_dirty=False)
 report['debug_assets'].append({'path':path,'changes':changes,'attack_debug':bool(cdo.get_editor_property('draw_attack_line_of_sight'))})
unreal.SystemLibrary.execute_console_command(w,'sw.Projectile.DebugLaunch 0')
with unreal.ScopedEditorTransaction('Arrange WJLast three boss ship encounters'):
 p.set_actor_rotation(unreal.Rotator(pitch=0,yaw=180,roll=0),False)
 origin=p.get_actor_location()
 stops=[unreal.StoryNode.MIDDLE_BOSS1_DEFEATED,unreal.StoryNode.MIDDLE_BOSS3_DEFEATED,unreal.StoryNode.FINAL_BOSS_DEFEATED]
 for name,distance,stop in zip(keep,[8000.,35000.,60000.],stops):
  a=ships[name]
  a.set_actor_location(unreal.Vector(origin.x-distance,origin.y,0),False,True)
  a.set_actor_rotation(unreal.Rotator(pitch=0,yaw=0,roll=0),False)
  # Distinct squads keep each encounter independent; bypass Final's campaign-only fleet gate in this test level.
  a.set_editor_property('squad_id','WJLast_'+name)
  c=a.get_component_by_class(unreal.BossEncounterComponent)
  assert c.get_editor_property('boss_class')
  c.set_editor_property('encounter_enabled',True)
  c.set_editor_property('encounter_trigger',unreal.BossEncounterTrigger.PLAYER_SHIP_SIGHT)
  c.set_editor_property('required_story_node',unreal.StoryNode.GAME_STARTED)
  c.set_editor_property('stop_after_story_node',stop)
  anchor=next(x for x in a.get_components_by_class(unreal.DeckWaypointComponent) if x.get_name()=='BossSpawnPoint')
  # Boss-only anchors preserve their authored usage flags.
  c.set_editor_property('boss_spawn_point_id',anchor.get_editor_property('waypoint_id'))
  for area in a.get_components_by_class(unreal.DeckWalkAreaComponent):
   area.set_editor_property('draw_debug_area',False);area.set_editor_property('draw_debug_connections',False)
  report['ships'].append({'label':name,'distance_m':distance/100,'location':str(a.get_actor_location()),'boss':c.get_editor_property('boss_class').get_path_name(),'anchor':c.get_editor_property('boss_spawn_point_id')})
 for name,a in ships.items():
  if name not in keep:
   report['removed'].append(name)
   assert s.destroy_actor(a)
assert len([a for a in s.get_all_level_actors() if isinstance(a,unreal.EnemyShip)])==3
assert unreal.EditorLoadingAndSavingUtils.save_map(w,'/Game/Level/Lvl_WJLast')
s.set_selected_level_actors([p]+[ships[n] for n in keep])
open(ROOT+'/Saved/WJLast_edit_report.json','w',encoding='utf8').write(json.dumps(report,indent=2))
print('WJLAST_EDIT_COMPLETE')





