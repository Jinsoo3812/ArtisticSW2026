import json
f=live_api['cannon_test'];rows=[]
for w,pc in zip(f['worlds'],f['pcs']):
    p=pc.get_controlled_pawn()
    rows.append({'world':unreal.SWAudioAuthoringLibrary.describe_audio_world(w),'pawn':p.get_name() if p else None,'is_character':isinstance(p,unreal.BasePlayer),'attached':p.get_attach_parent_actor().get_name() if p and p.get_attach_parent_actor() else None,'movement':str(p.get_editor_property('character_movement').movement_mode) if isinstance(p,unreal.BasePlayer) else None,'cannon_hides':[c.is_operator_hidden_for_controller(pc) for c in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.Cannon)]})
(OUT/'cannon_exit_checks.json').write_text(json.dumps(rows,indent=2));result=rows
