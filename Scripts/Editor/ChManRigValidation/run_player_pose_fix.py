"""Actual project automatic death correction and real UE captures."""
import unreal,json,time,math,traceback
from pathlib import Path
ROOT=Path(unreal.Paths.project_dir()).resolve()
assert (ROOT/'ArtisticSW2026.uproject').exists()
RAGDOLL_CASES=globals().get('REVIEW_CASES',[(label,30,mode) for label in ['Man','Woman'] for mode in ['idle','run','sprint','jump']])
RAGDOLL_FILENAME=globals().get('REVIEW_FILENAME','player_pose_fix.json')
base=ROOT/'Scripts/Editor/ChManRigValidation/run_ragdoll_pie.py'
exec(compile(base.read_text(encoding='utf8').replace('RUNNER=Runner()',''),str(base),'exec'))
OUT=ROOT/'Saved/PlayerRagdollPoseFix';OUT.mkdir(parents=True,exist_ok=True)
RESULT['project']=str(ROOT);RESULT['capture_method']='Unreal PIE world SceneCapture2D FinalColorLDR, not Blender reconstruction'
SHOTS=ROOT/'Saved/PlayerRagdollPoseFix/Screenshots';SHOTS.mkdir(parents=True,exist_ok=True)

class ReviewRunner(Runner):
    def __init__(self):
        self.capture_actor=None;self.capture_component=None;self.target=None;self.early_done=False
        super().__init__()
        self.deadline=time.monotonic()+1200
        self.floor_component.set_material(0,unreal.load_asset('/Engine/BasicShapes/BasicShapeMaterial'))
        for rotation,intensity in [(unreal.Rotator(-50,-25,0),3.5),(unreal.Rotator(-35,150,0),2.0)]:
            actor=ACTORS.spawn_actor_from_class(unreal.DirectionalLight,unreal.Vector(0,0,FLOOR+500),rotation)
            light=actor.get_component_by_class(unreal.DirectionalLightComponent);light.set_intensity(intensity)
        sky=ACTORS.spawn_actor_from_class(unreal.SkyLight,unreal.Vector(0,0,FLOOR+600))
        light=sky.get_component_by_class(unreal.SkyLightComponent)
        light.set_editor_property('source_type',unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP)
        light.set_editor_property('cubemap',unreal.load_asset('/Engine/MapTemplates/Sky/DaylightAmbientCubemap'))
        light.set_intensity(1.)
        actor=ACTORS.spawn_actor_from_class(unreal.SceneCapture2D,unreal.Vector(0,0,FLOOR+400))
        component=actor.get_component_by_class(unreal.SceneCaptureComponent2D)
        component.set_editor_property('capture_every_frame',False);component.set_editor_property('capture_on_movement',False)
    def next_case(self):
        super().next_case()
        if self.state!='testing':return
        self.early_done=False;self.final_done=False;self.case['screenshots']=[]
        self.case['requested_angles']={'shoulder_twist':25,'elbow_flexion':[0,40],'knee_flexion':[0,50]}
        unreal.AutomationLibrary.finish_loading_before_screenshot()
        if self.capture_actor:return
        captures=unreal.GameplayStatics.get_all_actors_of_class(self.world,unreal.SceneCapture2D);assert len(captures)==1
        self.capture_actor=captures[0]
        capture=self.capture_actor.get_component_by_class(unreal.SceneCaptureComponent2D);self.capture_component=capture
        self.target=unreal.RenderingLibrary.create_render_target2d(self.world,1280,900,unreal.TextureRenderTargetFormat.RTF_RGBA8)
        self.target.set_editor_property('target_gamma',2.2)
        capture.set_editor_property('texture_target',self.target)
        capture.set_editor_property('capture_source',unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR)
        capture.set_editor_property('capture_every_frame',False);capture.set_editor_property('capture_on_movement',False)
        capture.set_editor_property('fov_angle',42.0)
        pp=capture.get_editor_property('post_process_settings')
        pp.set_editor_property('override_auto_exposure_method',True);pp.set_editor_property('auto_exposure_method',unreal.AutoExposureMethod.AEM_MANUAL)
        pp.set_editor_property('override_auto_exposure_apply_physical_camera_exposure',True);pp.set_editor_property('auto_exposure_apply_physical_camera_exposure',False)
        pp.set_editor_property('override_auto_exposure_bias',True);pp.set_editor_property('auto_exposure_bias',0.)
        capture.set_editor_property('post_process_settings',pp)
    def screenshot(self,s,phase):
        points=[b['position'] for b in s['bodies']]
        low=[min(p[i] for p in points) for i in range(3)];high=[max(p[i] for p in points) for i in range(3)]
        center=unreal.Vector(*[(a+b)*.5 for a,b in zip(low,high)])
        views=[('angled',(.45,-.6,.8))]
        if phase=='settled':views.append(('top',(0,0,1)))
        for view,axis in views:
            norm=math.sqrt(sum(v*v for v in axis));axis=[v/norm for v in axis];forward=[-v for v in axis]
            right=[-forward[1],forward[0],0];norm=math.sqrt(sum(v*v for v in right))
            right=[v/norm for v in right] if norm>.001 else [0,1,0]
            up=[forward[1]*right[2]-forward[2]*right[1],forward[2]*right[0]-forward[0]*right[2],forward[0]*right[1]-forward[1]*right[0]]
            relative=[[p[0]-center.x,p[1]-center.y,p[2]-center.z] for p in points]
            extent=lambda basis:max(abs(sum(x*y for x,y in zip(p,basis))) for p in relative)
            tangent=math.tan(math.radians(21))
            distance=max((extent(up)+20)/(tangent*900/1280),(extent(right)+20)/tangent)+extent(axis)+20
            position=center+unreal.Vector(*[v*distance for v in axis]);rotation=unreal.MathLibrary.find_look_at_rotation(position,center)
            self.capture_actor.set_actor_location_and_rotation(position,rotation,False,True)
            self.capture_component.capture_scene()
            filename=self.case['character']+'_'+self.mode+'_'+phase+'_'+view+'.png'
            unreal.RenderingLibrary.export_render_target(self.world,self.target,str(SHOTS),filename)
            path=SHOTS/filename
            assert path.exists() and path.stat().st_size>10000,'Screenshot not exported: '+str(path)
            self.case['screenshots'].append({'phase':phase,'view':view,'file':str(path),'game_time':s['time']})
    def record(self,s,elapsed):
        super().record(s,elapsed)
        if elapsed>.25:
            ranges=self.case.setdefault('requested_joint_ranges',{})
            for j in s['joints']:
                name=j['child']
                if name.startswith('upperarm_'):value=j['twist'];meaning='shoulder_twist_deg'
                elif name.startswith('lowerarm_'):value=20+j['swing2'];meaning='elbow_flexion_deg'
                elif name.startswith('calf_'):value=25+j['swing2'];meaning='knee_flexion_deg'
                else:continue
                row=ranges.setdefault(name,{'meaning':meaning,'minimum':value,'maximum':value})
                row['minimum']=min(row['minimum'],value);row['maximum']=max(row['maximum'],value)
        if elapsed<=.25:self.case['initial_peak_limit_excess_deg']=max(self.case.get('initial_peak_limit_excess_deg',0),max(j['angular_excess_deg'] for j in s['joints']))
        if elapsed>.2 and not self.early_done:
            self.early_done=True
            self.case['transition_pose']=json.loads(LIB.snapshot_character(self.pawn))
            if self.mode in ['idle','jump']:self.screenshot(s,'transition')
        if elapsed>7.9 and not self.final_done:
            self.final_done=True
            visual=unreal.SWCorpsePoseLibrary.get_visual(self.pawn) if globals().get('POSE_FIX',True) else None
            if visual:
                errors=[]
                for b in s['bodies']:
                    p=visual.get_bone_location_by_name(b['bone'],unreal.BoneSpaces.WORLD_SPACE)
                    errors.append(math.dist([p.x,p.y,p.z],b['position']))
                self.case['max_visual_physical_error_cm']=max(errors)
                source=self.pawn.get_editor_property('mesh')
                self.case['material_slots']={'source':[str(source.get_material(i).get_path_name()) if source.get_material(i) else None for i in range(source.get_num_materials())],
                    'visual':[str(visual.get_material(i).get_path_name()) if visual.get_material(i) else None for i in range(visual.get_num_materials())]}
                assert self.case['material_slots']['source']==self.case['material_slots']['visual'] and all(self.case['material_slots']['visual'])
                before=[];after=[]
                def off_axis(p,a,b):
                    v=[b[i]-a[i] for i in range(3)];w=[p[i]-a[i] for i in range(3)];t=sum(x*y for x,y in zip(v,w))/sum(x*x for x in v)
                    return math.sqrt(sum((w[i]-t*v[i])**2 for i in range(3)))
                for side in ['l','r']:
                    for stem,end in [('upperarm','lowerarm'),('lowerarm','hand'),('thigh','calf'),('calf','foot')]:
                        a=source.get_socket_location(stem+'_'+side);b=source.get_socket_location(end+'_'+side)
                        for index in [1,2]:
                            name=stem+'_twist_'+str(index).zfill(2)+'_'+side
                            old=source.get_socket_location(name);new=visual.get_bone_location_by_name(name,unreal.BoneSpaces.WORLD_SPACE)
                            before.append(off_axis([old.x,old.y,old.z],[a.x,a.y,a.z],[b.x,b.y,b.z]))
                            after.append(off_axis([new.x,new.y,new.z],[a.x,a.y,a.z],[b.x,b.y,b.z]))
                self.case['source_twist_off_axis_cm']=max(before);self.case['corrected_twist_off_axis_cm']=max(after)
            self.screenshot(s,'settled')
    def finish(self):
        if not RESULT['errors'] and len(RESULT['cases'])==8:
            RESULT['completed']=True
        super().finish()

RUNNER=ReviewRunner()
