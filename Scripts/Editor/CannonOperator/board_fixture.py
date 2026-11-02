import json
worlds=unreal.SWAudioAuthoringLibrary.get_audio_pie_worlds();server=next(w for w in worlds if 'NetMode=2 ' in unreal.SWAudioAuthoringLibrary.describe_audio_world(w));worlds=[server,next(w for w in worlds if w!=server)]
pcs=[unreal.GameplayStatics.get_player_controller(w,0) for w in worlds]
assert all(pc and isinstance(pc.get_controlled_pawn(),unreal.BasePlayer) for pc in pcs),'Wait for players'
owners=[pc.get_controlled_pawn() for pc in pcs];names=[p.get_name() for p in owners]
for w in worlds:
    for ai in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.AIController):
        ai.stop_movement();brain=ai.get_editor_property('brain_component')
        if brain:brain.stop_logic('Cannon operator validation')
cannons=[]
for c in unreal.GameplayStatics.get_all_actors_of_class(server,unreal.Cannon):
    a=c.get_attach_parent_actor()
    while a and not isinstance(a,unreal.Ship):a=a.get_attach_parent_actor()
    if a and not isinstance(a,unreal.EnemyShip) and c.allows_player_control():cannons.append(c)
assert len(cannons)>=2
cannons=cannons[:2]
for name,c in zip(names,cannons):
    p=next(p for p in unreal.GameplayStatics.get_all_actors_of_class(server,unreal.BasePlayer) if p.get_name()==name)
    assert unreal.SWCannonOperatorAuthoringLibrary.queue_operator_board(p,c)
live_api['cannon_test']={'worlds':worlds,'pcs':pcs,'owners':owners,'names':names,'cannon_names':[c.get_name() for c in cannons]}
result={'owners':names,'cannons':[c.get_name() for c in cannons]}
(OUT/'cannon_board.json').write_text(json.dumps(result,indent=2))
