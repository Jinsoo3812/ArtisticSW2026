"""Fresh saved-level verification and real PIE guard lifecycle smoke check."""
import unreal,time,json,traceback
from pathlib import Path
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
OUT=Path(unreal.Paths.project_saved_dir())/'GuardedChestSetup'
result={'checks':[],'errors':[]}
# Re-read saved authoring without saving any assets.
exec(compile((Path(unreal.Paths.project_dir())/'Scripts/Editor/configure_guarded_chests.py').read_text(encoding='utf-8-sig'),'verify','exec'))
LEVEL=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
EDITOR=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
class Runner:
 def __init__(self):
  self.stage=0;self.next=time.monotonic()+2;self.deadline=time.monotonic()+180
  self.handle=unreal.register_slate_post_tick_callback(self.tick)
 def finish(self):
  (OUT/'pie.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
  unreal.unregister_slate_post_tick_callback(self.handle)
  unreal.EditorPythonScripting.set_keep_python_script_alive(False)
  unreal.SystemLibrary.quit_editor()
 def tick(self,dt):
  try:
   now=time.monotonic()
   if now>self.deadline:raise RuntimeError('PIE timeout')
   if now<self.next:return
   if self.stage==0:
    LEVEL.editor_request_begin_play();self.stage=1;self.next=now+8;return
   if self.stage==1:
    world=EDITOR.get_game_world()
    if not world or not LEVEL.is_in_play_in_editor():return
    spawners=unreal.GameplayStatics.get_all_actors_of_class(world,unreal.GroundEnemySpawner)
    for label,count in [('BP_ESpawner_Z1',3),('BP_ESpawner_Z2',5)]:
     s=next(s for s in spawners if s.get_actor_label()==label)
     bindings=unreal.GameplayStatics.get_all_actors_of_class(world,unreal.ChestSpawnPoint)
     matches=[p for p in bindings if any(b.get_editor_property('guard_spawner')==s for b in p.get_components_by_class(unreal.ChestGroundGuardBindingComponent))]
     assert len(matches)==1
     p=matches[0]
     c=p.get_spawned_actor()
     assert c and c.is_locked() and c.get_alive_guard_count()==count,(label,'initial guards',c.get_alive_guard_count() if c else None)
     assert not c.has_boss_guard() and not c.call_method("IsBossChest")
     enemies=list(p.get_editor_property('guard_characters'))
     assert len(enemies)==count
     row={'spawner':label,'initial_alive':c.get_alive_guard_count(),'initial_locked':c.is_locked(),'class':c.get_class().get_path_name(),'scale':c.get_actor_scale3d().export_text()}
     for i,e in enumerate(enemies):
      h=e.get_component_by_class(unreal.BaseHealthComponent)
      assert h
      h.start_death()
      assert c.get_alive_guard_count()==count-i-1
      assert c.is_locked()==(i<count-1),(label,'incorrect unlock after',i+1)
     row.update(final_alive=c.get_alive_guard_count(),final_locked=c.is_locked(),passed=True)
     result['checks'].append(row)
    LEVEL.editor_request_end_play();self.stage=2;self.next=now+2
   elif self.stage==2 and not LEVEL.is_in_play_in_editor():self.finish()
  except Exception:
   result['errors'].append(traceback.format_exc());unreal.log_error(result['errors'][-1]);self.finish()
RUNNER=Runner()


