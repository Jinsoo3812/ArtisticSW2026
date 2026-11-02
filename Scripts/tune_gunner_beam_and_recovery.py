"""Apply the requested timing without changing muzzle scale, radius, colour or damage."""
import unreal
from pathlib import Path
import shutil

ROOT = '/Game/GameplayAbilitySystem/Enemy/Balancing/T1/'
bp = unreal.load_asset(ROOT + 'BP_T1Boss_Enemy')
combat = unreal.get_default_object(bp.generated_class()).get_editor_property('combat_data')
aim = unreal.load_asset(ROOT + 'Animations/AS_T1Boss_RifleAim')
fire = unreal.load_asset(ROOT + 'Animations/AS_T1Boss_RifleFire')


def backup(asset):
    relative = asset.get_path_name().split('.')[0].removeprefix('/Game/') + '.uasset'
    original = Path(unreal.Paths.project_content_dir()) / relative
    copied = Path(unreal.Paths.project_saved_dir()) / 'GunnerRecoveryTimingBackup' / relative
    if original.exists() and not copied.exists():
        copied.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(original, copied)


backup(combat)
ray = combat.get_editor_property('ray')
ray.set_editor_property('pre_shot_show_time', 0.5)
combat.set_editor_property('ray', ray)
for name, recovery in [('basic', 0.35), ('burst', 0.5)]:
    pattern = combat.get_editor_property(name)
    pattern.set_editor_property('recovery_time', recovery)
    if name == 'basic':
        pattern.set_editor_property('post_attack_gap', 0.2)
    montage = pattern.get_editor_property('montage')
    if montage and montage.get_path_name().startswith(ROOT + 'Animations/AM_T1Boss_Rifle'):
        backup(montage)
        duration = 0.2
        if name == 'burst' and pattern.get_editor_property('shot_gap') > 0:
            duration = min(duration, pattern.get_editor_property('shot_gap'))
        if not unreal.GunnerAnimationEditorLibrary.configure_combat_montage(montage, aim, fire, duration):
            raise RuntimeError('Could not shorten firing recoil for ' + name)
        if not unreal.EditorAssetLibrary.save_loaded_asset(montage):
            raise RuntimeError('Could not save ' + montage.get_path_name())
    combat.set_editor_property(name, pattern)
if not unreal.EditorAssetLibrary.save_loaded_asset(combat):
    raise RuntimeError('Could not save Combat Data')
unreal.log('GUNNER_BEAM_WINDOW_AND_QUICK_RECOVERY_APPLIED')
