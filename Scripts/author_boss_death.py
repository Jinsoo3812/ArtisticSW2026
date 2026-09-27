"""Configure the authored Samurai boss death montage and death ability.

Run with UnrealEditor-Cmd -run=pythonscript -script=<absolute path> and
-EnablePlugins=PythonScriptPlugin,EditorScriptingUtilities.
"""
import unreal

MONTAGE = "/Game/Fab/Samurai/Animations/Montages/AM_Samurai_Death"
ABILITY = "/Game/GameplayAbilitySystem/Ability/Enemy/Boss/BPGA_BossDeath"
BOSS = "/Game/GameplayAbilitySystem/Enemy/BP_Ship_BossEnemy"


def required(path):
    asset = unreal.load_asset(path)
    if not asset:
        raise RuntimeError(f"Missing required asset: {path}")
    return asset


boss = required(BOSS)
boss_cdo = unreal.get_default_object(boss.generated_class())
mesh = boss_cdo.get_editor_property("mesh").get_editor_property("skeletal_mesh_asset")
# Preserve the artist-authored animation, sections and blend profile. The one
# setting required for the corpse pose is disabling automatic blend-out.
montage = required(MONTAGE)
if montage.get_editor_property("skeleton") != mesh.get_editor_property("skeleton"):
    raise RuntimeError("Boss death montage must use the boss mesh skeleton")
montage.set_editor_property("enable_auto_blend_out", False)

shared = required("/Game/Characters/Mannequins/Anims/Death/AM_Death")
slots = unreal.AnimationLibrary.get_montage_slot_names(montage)
shared_slots = unreal.AnimationLibrary.get_montage_slot_names(shared)
if slots != shared_slots:
    raise RuntimeError(f"Death slot mismatch: boss={slots}, existing={shared_slots}")

ability = required(ABILITY)
cdo = unreal.get_default_object(ability.generated_class())
cdo.set_editor_property("death_montage", montage)
cdo.set_editor_property("death_montage_play_rate", 1.0)
cdo.set_editor_property("death_montage_start_section", "None")
cdo.set_editor_property("stop_death_montage_when_ability_ends", False)
cdo.set_editor_property("finish_death_when_montage_ends", True)
cdo.set_editor_property("auto_finish_death_without_montage", True)
cdo.set_editor_property("disable_movement", True)
cdo.set_editor_property("disable_capsule_collision", True)
for asset in (montage, ability):
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError(f"Failed to save {asset.get_path_name()}")
unreal.log(f"BOSS_DEATH_AUTHORED montage={montage.get_path_name()} duration={montage.get_play_length()} slots={slots}; existing boss lifespan={boss_cdo.get_editor_property('corpse_lifetime_after_death_finished')}")
