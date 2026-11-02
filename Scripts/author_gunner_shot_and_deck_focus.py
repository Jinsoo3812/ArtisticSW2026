"""Create tunable shot visuals and attach focus refresh to used deck/boss combat tasks."""
import unreal
from pathlib import Path
import shutil

ROOT = '/Game/GameplayAbilitySystem/Enemy/Balancing/T1/'
VFX = ROOT + 'VFX/'


def backup(asset):
    package = asset.get_path_name().split('.')[0]
    relative = package.removeprefix('/Game/') + '.uasset'
    source = Path(unreal.Paths.project_content_dir()) / relative
    destination = Path(unreal.Paths.project_saved_dir()) / 'GunnerShotFocusBackup' / relative
    if source.exists() and not destination.exists():
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)


def save(asset):
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset):
        raise RuntimeError('Could not save ' + asset.get_path_name())


def shot_material():
    path = VFX + 'M_T1Boss_ShotCylinder'
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        return unreal.load_asset(path)
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        'M_T1Boss_ShotCylinder', VFX.rstrip('/'), unreal.Material, unreal.MaterialFactoryNew())
    material.set_editor_property('blend_mode', unreal.BlendMode.BLEND_ADDITIVE)
    material.set_editor_property('two_sided', True)
    material.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    edit = unreal.MaterialEditingLibrary
    color = edit.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -600, 0)
    color.set_editor_property('parameter_name', 'ShotColor')
    color.set_editor_property('default_value', unreal.LinearColor(1, 0.35, 0, 1))
    brightness = edit.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -600, 160)
    brightness.set_editor_property('parameter_name', 'Brightness')
    brightness.set_editor_property('default_value', 6.0)
    fade = edit.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -300, 260)
    fade.set_editor_property('parameter_name', 'Fade')
    fade.set_editor_property('default_value', 1.0)
    multiply = edit.create_material_expression(material, unreal.MaterialExpressionMultiply, -300, 0)
    edit.connect_material_expressions(color, '', multiply, 'A')
    edit.connect_material_expressions(brightness, '', multiply, 'B')
    edit.connect_material_property(multiply, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    edit.connect_material_property(fade, '', unreal.MaterialProperty.MP_OPACITY)
    edit.recompile_material(material)
    save(material)
    return material


def main():
    material = shot_material()
    flash_path = VFX + 'NS_T1Boss_MuzzleFlash'
    if unreal.EditorAssetLibrary.does_asset_exist(flash_path):
        flash = unreal.load_asset(flash_path)
    else:
        flash = unreal.EditorAssetLibrary.duplicate_asset(
            '/Game/Resources_Assets/Shooter_VFXPack/Particles/MuzzleFlashes/P_MuzzleFlash_01', flash_path)
    if not isinstance(flash, unreal.NiagaraSystem):
        raise RuntimeError('Muzzle source is not a Niagara system')
    if not unreal.GunnerCombatEditorLibrary.simplify_muzzle_flash(flash):
        raise RuntimeError('Could not simplify muzzle flash')
    save(flash)
    bp = unreal.load_asset(ROOT + 'BP_T1Boss_Enemy')
    combat = unreal.get_default_object(bp.generated_class()).get_editor_property('combat_data')
    backup(combat)
    ray = combat.get_editor_property('ray')
    ray.set_editor_property('shot_material', material)
    ray.set_editor_property('shot_color', unreal.LinearColor(1, 0.35, 0, 1))
    ray.set_editor_property('shot_radius', 4.0)
    ray.set_editor_property('shot_brightness', 6.0)
    ray.set_editor_property('pre_shot_show_time', 0.5)
    ray.set_editor_property('shot_lifetime', 1.0)
    combat.set_editor_property('ray', ray)
    combat.set_editor_property('muzzle_flash', flash)
    combat.set_editor_property('muzzle_flash_scale', 0.35)
    save(combat)

    # Discover the actual combat routes used by every saved deck/boss Blueprint,
    # including tier variants and the gunner's separate subtree.
    trees = {}
    assets = unreal.AssetRegistryHelpers.get_asset_registry().get_assets_by_path(
        '/Game/GameplayAbilitySystem/Enemy', recursive=True)
    for data in assets:
        if str(data.asset_class_path.asset_name) != 'Blueprint':
            continue
        asset = data.get_asset()
        cls = asset.generated_class() if isinstance(asset, unreal.Blueprint) else None
        if not cls:
            continue
        cdo = unreal.get_default_object(cls)
        if not isinstance(cdo, (unreal.DeckEnemy, unreal.ShipBossEnemy)):
            continue
        behavior = cdo.get_editor_property('behavior_set')
        tree = behavior.find_subtree(unreal.EnemyAIState.COMBAT) if behavior else None
        if tree:
            trees[tree.get_path_name()] = tree
    # Also keep maintained no-alarm/legacy deck and boss combat variants usable.
    for path in unreal.EditorAssetLibrary.list_assets('/Game/GameplayAbilitySystem/Enemy/AI/SubTree', recursive=True):
        if 'Combat' in path and any(name in path for name in ['/DeckMelee/', '/DeckRanged/', '/RogueBoss/', '/SamuraiBoss/', '/T1Shipboss/', '/T3ShipBoss/']):
            tree = unreal.load_asset(path)
            if isinstance(tree, unreal.BehaviorTree):
                trees[tree.get_path_name()] = tree
    for path, tree in sorted(trees.items()):
        backup(tree)
        count = unreal.GunnerCombatEditorLibrary.add_task_focus(tree)
        if count < 0:
            raise RuntimeError('Task focus authoring failed: ' + path)
        save(tree)
        unreal.log('DECK_FOCUS_APPLIED ' + path + ' added=' + str(count))
    unreal.log('GUNNER_SHOT_AND_DECK_FOCUS_APPLIED trees=' + str(len(trees)))


main()
