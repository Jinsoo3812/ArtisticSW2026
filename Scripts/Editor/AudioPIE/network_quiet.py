f=live_api['network_fixture'];counts=[]
for w in f['worlds']:
 n=0
 for a in f['allof'](w,unreal.BaseEnemy):
  a.get_component_by_class(unreal.SWFoleyComponent).set_editor_property('bEnabled',False)
  a.get_editor_property('character_movement').disable_movement()
  ai=a.get_controller()
  if isinstance(ai,unreal.AIController):
   ai.stop_movement()
   brain=ai.get_editor_property('brain_component')
   if brain:brain.stop_logic('Audio isolation')
  n+=1
 counts.append(n)
result={'muted_enemy_count':counts}
