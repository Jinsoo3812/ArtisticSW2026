import unreal

for path in [
    '/Game/Characters/Mannequins/Anims/Rifle/MF_Rifle_Idle_ADS',
    '/Game/Characters/Mannequins/Anims/Rifle/MM_Rifle_Fire',
    '/Game/Characters/Mannequins/Anims/Rifle/Walk/MF_Rifle_Walk_Fwd',
    '/Game/GameplayAbilitySystem/Enemy/Balancing/T1/BP_T1Boss_Enemy',
]:
    asset = unreal.load_asset(path)
    unreal.log('RIFLE_INSPECT ' + path + ' class=' + str(asset.get_class()))
    if isinstance(asset, unreal.AnimSequence):
        for name in ['skeleton', 'sequence_length', 'additive_anim_type', 'ref_pose_type', 'ref_pose_seq', 'ref_frame_index']:
            unreal.log('RIFLE_INSPECT ' + name + '=' + str(asset.get_editor_property(name)))
    elif isinstance(asset, unreal.Blueprint):
        mesh = unreal.get_default_object(asset.generated_class()).get_editor_property('mesh')
        unreal.log('RIFLE_INSPECT mesh=' + str(mesh.get_editor_property('skeletal_mesh_asset')))
        unreal.log('RIFLE_INSPECT anim_class=' + str(mesh.get_editor_property('anim_class')))
        unreal.log('RIFLE_INSPECT skeleton=' + str(mesh.get_editor_property('skeletal_mesh_asset').get_editor_property('skeleton')))
