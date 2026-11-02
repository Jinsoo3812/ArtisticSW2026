import json
from pathlib import Path
cfg=json.loads((Path(unreal.Paths.project_dir())/'docs/validation/Ship_Camera_Pillar_Selection.json').read_text())
rows=[]
for w in unreal.SWAudioAuthoringLibrary.get_audio_pie_worlds():
    ship=next(s for s in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.Ship) if not isinstance(s,unreal.EnemyShip) and not s.get_editor_property('hidden'))
    parts={c.get_name():c for c in ship.get_components_by_class(unreal.StaticMeshComponent)}
    simple=parts['DeckMesh_Simple'];proxy=parts['CameraObstacleMesh'];tf=ship.get_actor_transform()
    pillars=[]
    for p in cfg['pillars']:
        center=[(p['min'][i]+p['max'][i])/2 for i in range(3)]
        a=unreal.MathLibrary.transform_location(tf,unreal.Vector(center[0],center[1]-70,500))
        b=unreal.MathLibrary.transform_location(tf,unreal.Vector(center[0],center[1]+70,500))
        old=simple.sphere_trace_component(a,b,12.,False,False,False)
        new=proxy.sphere_trace_component(a,b,12.,False,False,False)
        pillars.append({'id':p['id'],'gameplay_hits':old is not None,'camera_proxy_hits':new is not None})
    walls=[]
    for x in [-300,100,700]:
        for side in [-1,1]:
            a=unreal.MathLibrary.transform_location(tf,unreal.Vector(x,0,200))
            b=unreal.MathLibrary.transform_location(tf,unreal.Vector(x,800*side,200))
            old=simple.sphere_trace_component(a,b,12.,False,False,False)
            new=proxy.sphere_trace_component(a,b,12.,False,False,False)
            walls.append({'x':x,'side':side,'original_hits':old is not None,'camera_proxy_hits':new is not None})
    rows.append({'world':unreal.SWAudioAuthoringLibrary.describe_audio_world(w),'pillars':pillars,'walls':walls,'proxy_mesh':proxy.static_mesh.get_path_name() if proxy.static_mesh else None,'proxy_enabled':str(proxy.get_collision_enabled()),'original_pawn_response':str(simple.get_collision_response_to_channel(unreal.CollisionChannel.ECC_PAWN)),'proxy_pawn_response':str(proxy.get_collision_response_to_channel(unreal.CollisionChannel.ECC_PAWN))})
result=rows
(OUT/'geometry_validation.json').write_text(json.dumps(result,indent=2))
