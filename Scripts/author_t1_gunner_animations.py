"""Author dedicated gunner animations; never edit the supplied Rifle sequences."""
import unreal

FOLDER = '/Game/GameplayAbilitySystem/Enemy/Balancing/T1/'
ANIMS = FOLDER + 'Animations/'
SOURCE = '/Game/Characters/Mannequins/Anims/Rifle/'


def required(path):
    asset = unreal.load_asset(path)
    if not asset:
        raise RuntimeError('Missing asset: ' + path)
    return asset


def copy_asset(source, destination):
    existing = unreal.load_asset(destination) if unreal.EditorAssetLibrary.does_asset_exist(destination) else None
    return existing or unreal.EditorAssetLibrary.duplicate_asset(source, destination)


def main():
    aim = copy_asset(SOURCE + 'MF_Rifle_Idle_ADS', ANIMS + 'AS_T1Boss_RifleAim')
    fire = copy_asset(SOURCE + 'MM_Rifle_Fire', ANIMS + 'AS_T1Boss_RifleFire')
    # Editor source keys are absolute poses. Recompress the dedicated copy as a
    # full pose, so it can share a full-body montage with the non-additive ADS.
    fire.set_editor_property('additive_anim_type', unreal.AdditiveAnimationType.AAT_NONE)
    fire.set_editor_property('ref_pose_type', unreal.AdditiveBasePoseType.ABPT_NONE)
    fire.set_editor_property('ref_pose_seq', None)
    for sequence in [aim, fire]:
        sequence.set_editor_property('enable_root_motion', False)
        if not unreal.EditorAssetLibrary.save_loaded_asset(sequence):
            raise RuntimeError('Could not save ' + sequence.get_path_name())

    blueprint = required(FOLDER + 'BP_T1Boss_Enemy')
    boss = unreal.get_default_object(blueprint.generated_class())
    mesh = boss.get_editor_property('mesh')
    combat = boss.get_editor_property('combat_data')
    if not combat:
        raise RuntimeError('Boss has no Combat Data')
    montages = {}
    for name, suffix in [('basic', 'Shot'), ('burst', 'Burst')]:
        montage_name = 'AM_T1Boss_Rifle' + suffix
        montage_path = ANIMS + montage_name
        montage = unreal.load_asset(montage_path) if unreal.EditorAssetLibrary.does_asset_exist(montage_path) else None
        if not montage:
            factory = unreal.AnimMontageFactory()
            factory.set_editor_property('source_animation', aim)
            montage = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
                montage_name, ANIMS.rstrip('/'), unreal.AnimMontage, factory)
        # Fit burst recoil into the gap so the next preparation cannot cut it off.
        gap = combat.get_editor_property(name).get_editor_property('shot_gap')
        duration = min(0.2, gap) if name == 'burst' and gap > 0 else 0.2
        if not unreal.GunnerAnimationEditorLibrary.configure_combat_montage(montage, aim, fire, duration):
            raise RuntimeError('Montage authoring failed: ' + name)
        montages[name] = montage
    abp_path = ANIMS + 'ABP_T1Boss_Rifle'
    current_abp_path = mesh.get_editor_property('anim_class').get_path_name().split('.')[0]
    if current_abp_path == abp_path:
        anim_bp = required(abp_path)
    else:
        anim_bp = copy_asset(current_abp_path, abp_path)
    if not unreal.GunnerAnimationEditorLibrary.connect_combat_slot(anim_bp):
        raise RuntimeError('Could not connect montage slot while preserving locomotion')
    for montage in montages.values():
        if not unreal.GunnerAnimationEditorLibrary.audit_combat_assets(anim_bp, montage):
            raise RuntimeError('Animation asset audit failed')
    for name, default_lock in [('basic', 0.6), ('burst', 0.35)]:
        pattern = combat.get_editor_property(name)
        pattern.set_editor_property('montage', montages[name])
        pattern.set_editor_property('prepare_section', 'Aim')
        pattern.set_editor_property('fire_section', 'Fire')
        pattern.set_editor_property('recovery_section', 'Recover')
        pattern.set_editor_property('recovery_time', 0.35 if name == 'basic' else 0.5)
        if name == 'basic':
            pattern.set_editor_property('post_attack_gap', 0.2)
        if pattern.get_editor_property('lock_time') <= 0:
            pattern.set_editor_property('lock_time', min(default_lock, pattern.get_editor_property('prepare_time')))
        combat.set_editor_property(name, pattern)
    ray = combat.get_editor_property('ray')
    ray.set_editor_property('color', unreal.LinearColor(1, 0, 0, 1))
    ray.set_editor_property('locked_color', unreal.LinearColor(1, 0, 0, 1))
    ray.set_editor_property('shot_color', unreal.LinearColor(1, 0.35, 0, 1))
    ray.set_editor_property('pre_shot_show_time', 0.5)
    ray.set_editor_property('shot_lifetime', 1.0)
    combat.set_editor_property('ray', ray)
    mesh.set_editor_property('animation_mode', unreal.AnimationMode.ANIMATION_BLUEPRINT)
    mesh.set_editor_property('anim_class', anim_bp.generated_class())
    unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
    # Compilation can reinstate native component templates. Write the saved
    # default on the current CDO, rather than a runtime-only animation instance.
    saved_mesh = unreal.get_default_object(blueprint.generated_class()).get_editor_property('mesh')
    saved_mesh.set_editor_property('animation_mode', unreal.AnimationMode.ANIMATION_BLUEPRINT)
    saved_mesh.set_editor_property('anim_class', anim_bp.generated_class())

    # Only the slot registry is extended on the shared skeleton; original clips
    # and source AnimBP stay unchanged.
    skeleton = aim.get_editor_property('skeleton')
    for asset in [skeleton, *montages.values(), anim_bp, combat, blueprint]:
        if not unreal.EditorAssetLibrary.save_loaded_asset(asset):
            raise RuntimeError('Could not save ' + asset.get_path_name())
    unreal.log('T1_GUNNER_ANIMATIONS_APPLIED')


main()
