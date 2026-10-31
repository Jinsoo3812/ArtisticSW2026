import json
rows=[]
for w in unreal.SWAudioAuthoringLibrary.get_audio_pie_worlds():
    for p in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.BasePlayer):
        c=p.get_attach_parent_actor()
        if not isinstance(c,unreal.Cannon):continue
        ship=c.get_attach_parent_actor()
        while ship and not isinstance(ship,unreal.Ship):ship=ship.get_attach_parent_actor()
        if not ship:continue
        feet=[]
        for n in ['left_foot_plant','right_foot_plant']:
            pos=c.get_editor_property(n).get_world_location();up=p.get_actor_up_vector()
            hits=[]
            for comp in ship.get_components_by_class(unreal.StaticMeshComponent):
                if comp.get_name() not in ['DeckMesh_Simple','DeckMesh_Complex']:continue
                hit=comp.line_trace_component(pos+up*80,pos-up*80,True,False,False)
                if hit:
                    loc=hit[0];delta=pos-loc
                    hits.append({'component':comp.get_name(),'above_deck_cm':delta.x*up.x+delta.y*up.y+delta.z*up.z})
            feet.append({'point':n,'hits':hits})
        rows.append({'world':unreal.SWAudioAuthoringLibrary.describe_audio_world(w),'player':p.get_name(),'feet':feet})
result=rows
(OUT/'cannon_floor_probe.json').write_text(json.dumps(rows,indent=2))
