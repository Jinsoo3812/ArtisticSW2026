import unreal,json
from pathlib import Path
mesh=unreal.load_asset('/Game/jiwon/Characters/ChWomanUE5/SKM_ChWoman_UE5')
anim=unreal.load_asset('/Game/Characters/UEFN_Mannequin/Animations/Run/M_Relaxed_Run_Stop_B_Lfoot')
report={'animation':anim.get_path_name(),'samples':[]}
for target in ['raw_source','compatible_woman']:
    for proportion in [0,.25,.5,.75,1]:
        opts=unreal.AnimPoseEvaluationOptions();opts.set_editor_property('should_retarget',target!='raw_source')
        if target!='raw_source':opts.set_editor_property('optional_skeletal_mesh',mesh)
        pose=unreal.AnimPoseExtensions.get_anim_pose_at_time(anim,anim.get_play_length()*proportion,opts)
        scales={}
        for name in ['thigh_r','calf_r','thigh_twist_01_r','calf_twist_02_r']:
            scale=unreal.AnimPoseExtensions.get_bone_pose(pose,name,unreal.AnimPoseSpaces.LOCAL).scale3d;scales[name]=[scale.x,scale.y,scale.z]
        report['samples'].append({'target':target,'proportion':proportion,'scales':scales})
(Path(unreal.Paths.project_saved_dir())/'ChWomanRigValidation/source_scale_audit.json').write_text(json.dumps(report,indent=2),encoding='utf8')
