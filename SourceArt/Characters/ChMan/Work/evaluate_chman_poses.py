import unreal, json, os, traceback, math
BASE=r'C:\Users\I\Documents\GitHub\ArtisticSW2026\SourceArt\Characters\ChMan'
report={'samples':[]}
def transform_data(t):
    return {'translation':[t.translation.x,t.translation.y,t.translation.z],
            'rotation':[t.rotation.x,t.rotation.y,t.rotation.z,t.rotation.w],
            'scale':[t.scale3d.x,t.scale3d.y,t.scale3d.z]}
try:
    mesh=unreal.load_asset('/Game/jiwon/Characters/ChManUE5/SKM_ChMan_UE5')
    skeleton=mesh.get_editor_property('skeleton')
    skeleton.set_editor_property('use_retarget_modes_from_compatible_skeleton',True)
    unreal.EditorAssetLibrary.save_loaded_asset(skeleton)
    ref=unreal.load_asset('/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple')
    clips={'Idle':'/Game/Characters/Mannequins/Anims/Unarmed/MM_Idle',
           'Walk':'/Game/Characters/Mannequins/Anims/Unarmed/Walk/MF_Unarmed_Walk_Fwd',
           'Jump':'/Game/Characters/Mannequins/Anims/Unarmed/Jump/MM_Jump'}
    for label,path in clips.items():
        anim=unreal.load_asset(path)
        duration=anim.get_play_length()
        for proportion in [0,.25,.5,.75,1]:
            time=duration*proportion
            for body,obj in [('ChMan',mesh),('Manny',ref)]:
                options=unreal.AnimPoseEvaluationOptions()
                options.set_editor_property('optional_skeletal_mesh',obj)
                options.set_editor_property('should_retarget',True)
                options.set_editor_property('retrieve_additive_as_full_pose',True)
                pose=unreal.AnimPoseExtensions.get_anim_pose_at_time(anim,time,options)
                names=unreal.AnimPoseExtensions.get_bone_names(pose)
                bones={str(n):transform_data(unreal.AnimPoseExtensions.get_bone_pose(pose,n,unreal.AnimPoseSpaces.WORLD)) for n in names}
                report['samples'].append({'clip':label,'body':body,'time':time,'bones':bones})
                if proportion == 0 and label == 'Idle':
                    report.setdefault('reference_bones',{})[body]={str(n):transform_data(unreal.AnimPoseExtensions.get_ref_bone_pose(pose,n,unreal.AnimPoseSpaces.WORLD)) for n in names}
    # Recover the actual original texture sources for Blender preview/packing.
    textures={}
    for slot in mesh.get_editor_property('materials'):
        mat=slot.material_interface
        try:
            used=unreal.MaterialEditingLibrary.get_used_textures(mat)
        except Exception:
            used=[]
        textures[str(slot.material_slot_name)]=[]
        for tex in used:
            if not isinstance(tex,unreal.Texture2D):continue
            folder=BASE+'\\Textures';os.makedirs(folder,exist_ok=True)
            filename=folder+'\\'+tex.get_name()+'.tga'
            task=unreal.AssetExportTask();task.object=tex;task.filename=filename
            task.automated=True;task.prompt=False;task.replace_identical=True
            task.exporter=unreal.TextureExporterTGA()
            ok=unreal.Exporter.run_asset_export_task(task)
            textures[str(slot.material_slot_name)].append({'asset':tex.get_path_name(),'file':filename,'exported':ok})
    report['textures']=textures
except Exception:
    report['error']=traceback.format_exc()
with open(BASE+'\\Work\\ue_pose_samples.json','w',encoding='utf8') as f:json.dump(report,f,indent=2,ensure_ascii=False)
