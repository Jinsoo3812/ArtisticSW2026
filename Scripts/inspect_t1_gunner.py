import unreal

FOLDER = '/Game/GameplayAbilitySystem/Enemy/Balancing/T1/'
bp = unreal.load_asset('/Game/GameplayAbilitySystem/Enemy/Balancing/T1/BP_T1Boss_Enemy')
if not bp:
    raise RuntimeError('BP_T1Boss_Enemy not found')
cdo = unreal.get_default_object(bp.generated_class())
unreal.log('GUNNER_AUDIT class=' + str(cdo.get_class()))
for name in ['combat_data', 'behavior_tree', 'behavior_set', 'default_weapon_tag',
             'equip_weapon_on_spawn', 'starting_abilities', 'default_stats_row']:
    try:
        unreal.log('GUNNER_AUDIT ' + name + '=' + str(cdo.get_editor_property(name)))
    except Exception as error:
        unreal.log_warning('GUNNER_AUDIT ' + str(error))
mesh = cdo.get_editor_property('mesh')
unreal.log('GUNNER_AUDIT mesh=' + str(mesh.get_editor_property('skeletal_mesh_asset')))
unreal.log('GUNNER_AUDIT anim_class=' + str(mesh.get_editor_property('anim_class')))
for socket in ['Gun_Grip', 'Gun_Muzzle']:
    unreal.log('GUNNER_AUDIT socket ' + socket + '=' + str(mesh.does_socket_exist(socket)))
component = cdo.get_editor_property('weapon_component')
unreal.log('GUNNER_AUDIT registry=' + str(component.get_editor_property('weapon_registry')))

if cdo.get_editor_property('combat_data'):
    assert cdo.get_editor_property('combat_data') == unreal.load_asset(FOLDER + 'T1_DA_GunnerCombat')
    assert cdo.get_editor_property('behavior_tree') == unreal.load_asset('/Game/GameplayAbilitySystem/Enemy/AI/BT_BossBase')
    behavior = cdo.get_editor_property('behavior_set')
    assert behavior == unreal.load_asset(FOLDER + 'T1_DA_GunnerAI')
    assert behavior.find_subtree(unreal.EnemyAIState.COMBAT) == unreal.load_asset(FOLDER + 'BT_T1Boss_GunnerCombat')
    registry = component.get_editor_property('weapon_registry')
    assert registry == unreal.load_asset(FOLDER + 'T1_DA_GunnerWeapons')
    definitions = registry.get_editor_property('weapon_definitions')
    assert len(definitions) == 1
    assert definitions[0].get_editor_property('weapon_actor_class') == unreal.load_class(None, FOLDER + 'BP_T1Boss_Gun.BP_T1Boss_Gun_C')
    assert not definitions[0].get_editor_property('ability_data').get_editor_property('granted_abilities')
    abilities = cdo.get_editor_property('starting_abilities')
    for class_name in ['GA_BossBasicAttack', 'GA_BossVanish', 'GA_BossVanishV2', 'GA_BossDashSlash', 'GA_BossKnockback']:
        assert unreal.load_class(None, '/Script/Enemy.' + class_name) not in abilities
    assert unreal.load_class(None, '/Script/Enemy.GA_BossGunBurst') not in abilities
    for class_name in ['GA_BossGunAttack']:
        assert sum(ability == unreal.load_class(None, '/Script/Enemy.' + class_name) for ability in abilities) == 1
    assert cdo.get_editor_property('equip_weapon_on_spawn')
    unreal.log('T1_GUNNER_SAVED_BINDINGS_OK')
