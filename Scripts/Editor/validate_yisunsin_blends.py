"""Verify actual NPC AnimGraph output through a slow/fast speed sweep in SIE.

Run in the open comparison map. Ends simulation and leaves the editor open.
Saves only a JSON report; no actors or asset settings are saved.
"""
import json, math, time
from pathlib import Path
import unreal
ROOT=Path(unreal.Paths.project_dir()).resolve()
OUT=ROOT/'SourceArt/Characters/YiSunSin/Audit/ue_blend_samples.json'
SPEEDS=[0,1,10,25,50,75,100,125,149,150,225,300,450,100,25,0]
CLASS=unreal.EditorAssetLibrary.load_blueprint_class('/Game/Chae_Asset/YiSunSinContent/NPC/Blueprints/BP_YiSunSin_NPC')
state={'started':time.monotonic(),'rows':[],'last':-1,'reference_component':{},'done':False}

def encode(t):
    return {'p':[t.translation.x,t.translation.y,t.translation.z],
            'q':[t.rotation.x,t.rotation.y,t.rotation.z,t.rotation.w],
            's':[t.scale3d.x,t.scale3d.y,t.scale3d.z]}

def finish(error=None):
    state['done']=True
    rows=state['rows']
    report={'passed':error is None,'error':error,'speeds':SPEEDS,'samples':len(rows),
        'actual_runtime_anim_graph':True,'reference_component':state['reference_component'],'rows':rows}
    if rows:
        report['root_scale_range']=[min(v for r in rows for v in r['bones']['spine']['s']),max(v for r in rows for v in r['bones']['spine']['s'])]
        report['upper_body_height_range_cm']=[min(r['upper_body_height_cm'] for r in rows),max(r['upper_body_height_cm'] for r in rows)]
    OUT.write_text(json.dumps(report,indent=2),encoding='utf-8')
    unreal.unregister_slate_post_tick_callback(state['handle'])
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()
    unreal.log('NPC_BLEND_RUNTIME_RESULT '+str({k:v for k,v in report.items() if k not in {'rows','reference_component'}}))

def tick(delta):
    if state['done']: return
    try:
        if time.monotonic()-state['started']>90: finish('Simulation timeout'); return
        world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
        if not world: return
        actors=unreal.GameplayStatics.get_all_actors_of_class(world,CLASS)
        if not actors: return
        npc=actors[0]; comp=npc.get_editor_property('mesh'); movement=npc.get_editor_property('character_movement')
        now=unreal.GameplayStatics.get_time_seconds(world)
        if 'game_start' not in state:
            state['game_start']=now
            for floor in unreal.GameplayStatics.get_all_actors_of_class(world,unreal.StaticMeshActor):
                if floor.get_actor_label() == 'Preview floor':
                    floor.set_actor_scale3d(unreal.Vector(100,100,.1))
            comp.set_editor_property('visibility_based_anim_tick_option',unreal.VisibilityBasedAnimTickOption.ALWAYS_TICK_POSE_AND_REFRESH_BONES)
            comp.set_component_tick_enabled(True)
            state['names']=[str(comp.get_bone_name(i)) for i in range(comp.get_num_bones())]
            refs={}
            for i,name in enumerate(state['names']):
                local=comp.get_ref_pose_transform(i); parent=str(comp.get_parent_bone(name))
                refs[name]=unreal.MathLibrary.compose_transforms(local,refs[parent]) if parent in refs else local
            state['reference_component']={n:encode(t) for n,t in refs.items()}
        elapsed=now-state['game_start']; index=int(elapsed/1.4)
        if index>=len(SPEEDS):
            assert len(state['rows'])>150
            finish();return
        speed=SPEEDS[index]
        movement.set_editor_property('max_walk_speed',float(speed))
        if speed:
            # Reverse in the second half to remain on the small comparison floor.
            npc.add_movement_input(unreal.Vector(1 if index<11 else -1,0,0),1.0,True)
        else: movement.stop_movement_immediately()
        if elapsed-state['last']<.075 or elapsed%1.4<.3: return
        state['last']=elapsed
        bones={n:encode(comp.get_socket_transform(n,unreal.RelativeTransformSpace.RTS_COMPONENT)) for n in state['names']}
        root=bones['spine']; height=bones['spine_006']['p'][2]-min(bones['foot_L']['p'][2],bones['foot_R']['p'][2])
        assert max(abs(s-100) for s in root['s'])<.02,(speed,root)
        assert 90<height<240,(speed,height)
        velocity=npc.get_velocity()
        state['rows'].append({'speed':speed,'actual_speed':math.hypot(velocity.x,velocity.y),
            'anim_speed':comp.get_anim_instance().get_editor_property('npc_ground_speed'),
            'time':elapsed,'upper_body_height_cm':height,'bones':bones})
    except Exception as exc: finish(str(exc))

state['handle']=unreal.register_slate_post_tick_callback(tick)
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_play_simulate()
