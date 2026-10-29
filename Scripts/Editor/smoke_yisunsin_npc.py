"""Full-editor SIE smoke: idle, walk, stop, run, and repeat. Saves no assets."""
import json
import math
import time
from pathlib import Path
import unreal

ROOT = Path(unreal.Paths.project_dir()).resolve()
OUT = ROOT/'SourceArt/Characters/YiSunSin/Audit/runtime_smoke.json'
NPC = '/Game/Chae_Asset/YiSunSinContent/NPC'
CLASS = unreal.EditorAssetLibrary.load_blueprint_class(NPC+'/Blueprints/BP_YiSunSin_NPC')
state = {'start': time.monotonic(), 'running': False, 'rows': [], 'last_sample': -1,
         'report': {'phases': ['idle','walk','stop','run','idle','walk','run','idle']}}
phases = [('idle',.5,0,0), ('walk',.8,150,1), ('stop',.4,0,0), ('run',.5,450,-1),
          ('idle',.4,0,0), ('walk',.8,150,1), ('run',.4,450,-1), ('idle',.5,0,0)]

def finish(error=None):
    if state.get('done'):
        return
    state['done'] = True
    state['report']['rows'] = state['rows']
    state['report']['error'] = error
    state['report']['passed'] = error is None
    OUT.write_text(json.dumps(state['report'],indent=2),encoding='utf-8')
    unreal.unregister_slate_post_tick_callback(state['handle'])
    unreal.EditorLevelLibrary.editor_end_play()
    unreal.SystemLibrary.quit_editor()

def tick(delta):
    try:
        if time.monotonic()-state['start'] > 90:
            finish('Timed out waiting for editor simulation')
            return
        world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
        if not world:
            return
        actors = unreal.GameplayStatics.get_all_actors_of_class(world, CLASS)
        if not actors:
            return
        npc = actors[0]
        component = npc.get_editor_property('mesh')
        movement = npc.get_editor_property('character_movement')
        now = unreal.GameplayStatics.get_time_seconds(world)
        if not state['running']:
            # The hidden review editor may never render this component. Force pose
            # evaluation on the PIE instance only so this tests the graph itself.
            component.set_editor_property('visibility_based_anim_tick_option',
                unreal.VisibilityBasedAnimTickOption.ALWAYS_TICK_POSE_AND_REFRESH_BONES)
            component.set_component_tick_enabled(True)
            state['running'] = True
            state['game_start'] = now
        elapsed = now-state['game_start']
        cursor = 0
        selected = None
        for index, (name,duration,speed,direction) in enumerate(phases):
            cursor += duration
            if elapsed < cursor:
                selected = (index,name,speed,direction)
                break
        if selected is None:
            moving = [r for r in state['rows'] if r['phase'] in {'walk','run'}]
            if not moving or max(r['speed'] for r in moving) < 100:
                finish('NPC did not move during the input phases')
                return
            foot_ranges = [max(r['left_foot_component'][axis] for r in moving)-
                           min(r['left_foot_component'][axis] for r in moving) for axis in range(3)]
            if max(foot_ranges) < 1:
                finish('Locomotion animation did not change the leg pose')
                return
            state['report']['max_speed_cm_per_second'] = max(r['speed'] for r in moving)
            state['report']['left_foot_component_ranges_cm'] = foot_ranges
            state['report']['test_instance_forced_pose_evaluation'] = True
            finish()
            return
        index,name,speed,direction = selected
        if speed:
            movement.set_editor_property('max_walk_speed',float(speed))
            npc.add_movement_input(unreal.Vector(float(direction),0,0),1.0,True)
        else:
            movement.stop_movement_immediately()
        if elapsed-state['last_sample'] >= .05:
            state['last_sample'] = elapsed
            instance = component.get_anim_instance()
            if not instance or instance.get_class().get_name() != 'ABP_YiSunSin_NPC_C':
                finish('NPC animation instance was not active')
                return
            position = npc.get_actor_location()
            velocity = npc.get_velocity()
            foot = component.get_socket_transform('foot_L',unreal.RelativeTransformSpace.RTS_COMPONENT).translation
            values = [position.x,position.y,position.z,foot.x,foot.y,foot.z]
            if not all(math.isfinite(v) for v in values):
                finish('Non-finite actor or bone transform')
                return
            state['rows'].append({'time':elapsed,'phase':name,'phase_index':index,
                                  'speed':math.hypot(velocity.x,velocity.y),
                                  'position':[position.x,position.y,position.z],
                                  'left_foot_component':[foot.x,foot.y,foot.z]})
    except Exception as exc:
        finish(str(exc))

state['handle'] = unreal.register_slate_post_tick_callback(tick)
unreal.EditorLevelLibrary.editor_play_simulate()
