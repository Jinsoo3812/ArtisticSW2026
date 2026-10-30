import json
f=live_api['cannon_test'];rows=[]
for w in f['worlds']:
    for c in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.Cannon):
        for name in ['base_mesh','barrel_mesh']:
            comp=c.get_editor_property(name)
            response=comp.get_collision_response_to_channel(unreal.CollisionChannel.ECC_FOOT_PLACEMENT)
            assert response==unreal.CollisionResponseType.ECR_IGNORE,(c.get_name(),name,str(response))
            rows.append({'actor':c.get_path_name(),'component':name,'foot_response':str(response),'collision':str(comp.get_collision_enabled())})
(OUT/'cannon_ground_policy.json').write_text(json.dumps(rows,indent=2));result={'components_checked':len(rows)}
