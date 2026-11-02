"""Real water entry -> actual port/starboard handler -> mapped C input, two PIE owners."""
import json
scripts=OUT.parent.parent/'LiveScripts'
exec(compile((scripts/'setup_pie.py').read_text(encoding='utf-8-sig'),'setup-pie','exec'),{'unreal':unreal,'OUT':OUT,'live_api':live_api})
worlds=None;began=None;current=-1;boarded=False;done=False;last=-1.;rows=[];cases=[]
lib=unreal.SWShipBoardingValidationLibrary

def state(p):return json.loads(lib.describe_roll_state(p))
def on_tick(delta):
 global worlds,began,current,boarded,done,last
 if done:return
 if worlds is None:
  ws=unreal.SWAudioAuthoringLibrary.get_audio_pie_worlds()
  if len(ws)!=2:return
  server=next(w for w in ws if 'NetMode=2 ' in unreal.SWAudioAuthoringLibrary.describe_audio_world(w));ws=[server,next(w for w in ws if w!=server)]
  ps=[unreal.GameplayStatics.get_player_controller(w,0) for w in ws]
  if not all(pc and isinstance(pc.get_controlled_pawn(),unreal.BasePlayer) for pc in ps):return
  ships=unreal.GameplayStatics.get_all_actors_of_class(server,unreal.Ship)
  own=next(s for s in ships if not isinstance(s,unreal.EnemyShip) and not s.get_editor_property('hidden'))
  # A dedicated real Blueprint ship avoids selecting an idle, hidden pooled bundle.
  enemy_class=unreal.load_class(None,'/Game/Blueprints/Ship/Enemy_Ship/Blueprints/BP_EnemyShip.BP_EnemyShip_C')
  transform=unreal.Transform(location=own.get_actor_location()+unreal.Vector(4000,0,0))
  gameplay=unreal.get_default_object(unreal.GameplayStatics)
  enemy=gameplay.call_method('BeginDeferredActorSpawnFromClass',args=(server,enemy_class,transform,unreal.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN,None,unreal.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
  gameplay.call_method('FinishSpawningActor',args=(enemy,transform,unreal.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
  assert enemy
  worlds=ws;live_api['boarding_fixture']={'worlds':ws,'pcs':ps,'names':[pc.get_controlled_pawn().get_name() for pc in ps],'ships':[own,enemy]}
  for w in ws:
   for ai in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.AIController):
    ai.stop_movement();brain=ai.get_editor_property('brain_component')
    if brain:brain.stop_logic('Bounded boarding roll diagnostic')
  sub=unreal.get_default_object(unreal.load_class(None,'/Script/Engine.SubsystemBlueprintLibrary'))
  f=live_api['boarding_fixture'];f['inputs']=[sub.call_method('GetLocalPlayerSubSystemFromPlayerController',args=(pc,unreal.EnhancedInputLocalPlayerSubsystem.static_class())) for pc in ps];f['actions']=[]
  for pc in ps:
   config=pc.get_controlled_pawn().get_editor_property('default_input_config')
   actions=[a.get_editor_property('input_action') for a in config.get_editor_property('key_input_actions') if str(a.get_editor_property('key_tag').get_editor_property('tag_name'))=='Key.Default.C']
   assert len(actions)==1,actions
   f['actions'].append(actions[0])
  began=unreal.GameplayStatics.get_time_seconds(server)
 f=live_api['boarding_fixture'];server=worlds[0];now=unreal.GameplayStatics.get_time_seconds(server);round=int((now-began)/14.);elapsed=(now-began)%14.
 if round>=4:
  for inp,act in zip(f['inputs'],f['actions']):inp.inject_input_vector_for_action(act,unreal.Vector(),[],[])
  (OUT/'boarding_roll_samples.json').write_text(json.dumps(rows));(OUT/'boarding_roll_complete.json').write_text(json.dumps({'rounds':4,'cases':cases,'samples':len(rows)},indent=2));done=True;return
 players=[next(p for p in unreal.GameplayStatics.get_all_actors_of_class(server,unreal.BasePlayer) if p.get_name()==name) for name in f['names']]
 if current!=round:
  current=round;boarded=False
  for i,p in enumerate(players):
   assert lib.queue_relocate(p,f['ships'][0].get_actor_location()+unreal.Vector(-2500,5500+700*i,1000))
  (OUT/'boarding_progress.json').write_text(json.dumps({'round':round,'stage':'water'}))
 if elapsed>=8 and not boarded:
  for i,p in enumerate(players):
   snapshot=state(p);assert snapshot['swimming'],snapshot
   ship=f['ships'][(i+round)%2];assert lib.queue_sea_board(p,ship,bool(i))
   cases.append({'round':round,'player':p.get_name(),'ship':ship.get_name(),'enemy':isinstance(ship,unreal.EnemyShip),'starboard':bool(i),'water':snapshot})
  boarded=True;(OUT/'boarding_progress.json').write_text(json.dumps({'round':round,'stage':'C input'}))
 held=(10<=elapsed<10.15) or (11.5<=elapsed<11.65)
 for inp,act in zip(f['inputs'],f['actions']):inp.inject_input_vector_for_action(act,unreal.Vector(1 if held else 0,0,0),[],[])
 if now-last<.05:return
 last=now
 for peer,w in enumerate(worlds):
  for p in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.BasePlayer):
   if p.get_name() in f['names']:
    d=state(p);d.update(time=now,round=round,elapsed=elapsed,peer=peer,C_held=held);rows.append(d)
result={'seconds':56,'scope':'two owners, both ship types, both boarding handlers, repeated water transitions and mapped C input'}
