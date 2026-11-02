"""Accumulate every native result, then replay the real two-owner sea-boarding flow."""
import json,re,time
from pathlib import Path
scripts=OUT.parent.parent/'LiveScripts'
m=re.search(r'-abslog=(?:"([^"]+)"|(\S+))',unreal.SystemLibrary.get_command_line(),re.I)
assert m
log=Path(m.group(1) or m.group(2));offset=log.stat().st_size;started=time.monotonic();last=0.;carry='';results={};done=False;phase='native';scope=None
filters='ArtisticSW.GAS+ArtisticSW.Crafting+ArtisticSW.Inventory+ArtisticSW.Interaction+ArtisticSW.Lifecycle+ArtisticSW.Swimming+ArtisticSW.Cannon.Operator+ArtisticSW.Animation+ArtisticSW.PlayerBow+ArtisticSW.Weapon+ArtisticSW.SharedStorage+ArtisticSW.NPCDialogue+ArtisticSW.PlayerSkill+ArtisticSW.Player.Death+ArtisticSW.GameMode.PlayerCorpse+ArtisticSW.Camera.ManualZoom+ArtisticSW.Chest.Pool+ArtisticSW.ShipUpgrade+ArtisticSW.UI.SkillQuickSlot+ArtisticSW.WaterBomb'
unreal.SystemLibrary.execute_console_command(live_api['EDITOR'].get_editor_world(),'Automation RunTests '+filters)

def save(name,value):(OUT/name).write_text(json.dumps(value,indent=2))
def on_tick(delta):
 global offset,last,carry,done,phase,scope
 if done:return
 if phase=='native':
  now=time.monotonic()
  if now-last<.25:return
  last=now;assert now-started<300,'Native regression timeout'
  with log.open('rb') as f:f.seek(offset);chunk=f.read();offset=f.tell()
  text=carry+chunk.decode('utf-8',errors='replace');lines=text.split('\n');carry=lines.pop()
  for line in lines:
   match=re.search(r'Test Completed\. Result=\{(\w+)\}.*Path=\{([^}]+)\}',line)
   if match:results[match.group(2)]=match.group(1)
  if 'Automation Test Queue Empty' not in text:return
  failed=[name for name,result in results.items() if result!='Success']
  summary={'count':len(results),'failed':failed,'results':results,'filters':filters,'log':str(log)};save('lifecycle_native.json',summary)
  assert results,'No native tests executed'
  if failed:done=True;save('lifecycle_complete.json',{'native_passed':False,'failed':failed});return
  phase='boarding';scope={'unreal':unreal,'OUT':OUT,'live_api':live_api,'result':None}
  exec(compile((scripts/'boarding_roll.py').read_text(encoding='utf-8-sig'),'boarding-replay','exec'),scope)
 else:
  scope['on_tick'](delta)
  if scope.get('done'):
   save('lifecycle_complete.json',{'native_passed':True,'native_count':len(results),'boarding_replay_complete':True});done=True
result={'requested':'broad gameplay lifecycle native regressions, then two-owner boarding replay','filters':filters}
