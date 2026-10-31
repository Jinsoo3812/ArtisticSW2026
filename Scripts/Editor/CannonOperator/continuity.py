import json
from pathlib import Path
import unreal

class CannonContinuityProbe:
    def __init__(self, worlds, names, diagnostics=False):
        self.worlds=worlds;self.names=names;self.rows=[];self.diagnostics=diagnostics
        self.ships=[next(s for s in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.Ship) if not isinstance(s,unreal.EnemyShip)) for w in worlds]
    def sample(self, time, stage):
        for peer,w in enumerate(self.worlds):
            for p in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.BasePlayer):
                if p.get_name() not in self.names:continue
                mesh=p.get_editor_property('mesh');points={};deck={}
                for b in ['pelvis','head','calf_l','calf_r','foot_l','foot_r']:
                    position=mesh.get_socket_location(b)
                    q=unreal.MathLibrary.inverse_transform_location(mesh.get_world_transform(),position)
                    points[b]=[q.x,q.y,q.z]
                    if b.startswith('foot'):
                        q=unreal.MathLibrary.inverse_transform_location(self.ships[peer].get_actor_transform(),position)
                        deck[b]=[q.x,q.y,q.z]
                q=unreal.MathLibrary.inverse_transform_location(self.ships[peer].get_actor_transform(),p.get_actor_location())
                row={'time':time,'stage':stage,'peer':peer,'player':p.get_name(),'points':points,'deck_feet':deck,'deck_root':[q.x,q.y,q.z],'mounted':isinstance(p.get_attach_parent_actor(),unreal.Cannon)}
                if self.diagnostics:
                    anim=mesh.get_anim_instance();state=p.get_editor_property('anim_state_component')
                    row['movement']=str(p.get_editor_property('character_movement').movement_mode)
                    row['velocity']=str(p.get_velocity());row['rotation']=str(p.get_actor_rotation())
                    controller=p.get_controller()
                    row['control_yaw']=controller.get_control_rotation().yaw if controller else None
                    row['actor_yaw']=p.get_actor_rotation().yaw
                    row['montage']=str(anim.get_current_active_montage())
                    for key in ['bIsHitReacting','bIsDodging']:
                        try:row[key]=p.get_editor_property(key)
                        except Exception:pass
                    for key in ['current_state','b_is_in_air','b_is_landing','b_should_turn_in_place','facing_delta_yaw']:
                        try:row[key]=str(state.get_editor_property(key))
                        except Exception:pass
                    for key in ['get_thread_safe_state_controller_presentation_state','get_thread_safe_state_controller_selected_animation','get_thread_safe_state_controller_selected_animation_elapsed_time']:
                        fn=getattr(anim,key,None)
                        if fn:
                            try:row[key]=str(fn())
                            except Exception:pass
                self.rows.append(row)
    def save(self, path):path.write_text(json.dumps(self.rows))
