"""Save the actual project test map and validate material/input bindings."""
import unreal,json
from pathlib import Path
ROOT=Path(unreal.Paths.project_dir()).resolve();assert (ROOT/'ArtisticSW2026.uproject').exists()
OUT=ROOT/'Saved/PlayerRagdollPoseFix';OUT.mkdir(parents=True,exist_ok=True)
apply=Path(__file__).with_name('apply_player_pose_fix.py')
exec(compile(apply.read_text(encoding='utf8'),str(apply),'exec'),{'__file__':str(apply),'__name__':'__main__'})
report={'project':str(ROOT),'materials':{},'keys':{}}
for label in ['Man','Woman']:
    mesh=unreal.load_asset('/Game/jiwon/Characters/Ch'+label+'UE5/SKM_Ch'+label+'_UE5')
    slots=mesh.get_editor_property('materials');rows=[]
    for slot in slots:
        material=slot.get_editor_property('material_interface')
        rows.append({'slot':str(slot.get_editor_property('material_slot_name')),'material':material.get_path_name() if material else None})
    report['materials'][label]=rows
    (OUT/'material_before.json').write_text(json.dumps(report['materials'],indent=2),encoding='utf8')
    if label=='Woman':
        # Validate the existing named project materials, not a new grey substitute.
        for index,slot in enumerate(slots):
            name=str(slot.get_editor_property('material_slot_name'))
            roles=[r for r in ['Clothes','Hair','Accessory','Bag','Body','Eyes','Shoes'] if r.lower() in name.lower()]
            assert len(roles)==1,(name,roles)
            material=unreal.load_asset('/Game/jiwon/Characters/ChGirlMtl/M_ChGirl'+roles[0]);assert material
            slot.set_editor_property('material_interface',material)
            slots[index]=slot
        mesh.set_editor_property('materials',slots)
        assert unreal.EditorAssetLibrary.save_loaded_asset(mesh,False)
        bound=mesh.get_editor_property('materials')
        assert all(s.get_editor_property('material_interface') for s in bound)
        report['materials']['Woman_bound']=[{'slot':str(s.material_slot_name),'material':s.material_interface.get_path_name()} for s in bound]

registry=unreal.AssetRegistryHelpers.get_asset_registry()
contexts=registry.get_assets_by_class(unreal.TopLevelAssetPath('/Script/EnhancedInput','InputMappingContext'),True)
conflicts=[]
for data in contexts:
    context=data.get_asset()
    for mapping in context.get_editor_property('mappings'):
        key=str(unreal.InputLibrary.get_key_display_name(mapping.get_editor_property('key')))
        if key in ['F7','F10']:conflicts.append({'context':context.get_path_name(),'key':key})
assert not conflicts,conflicts
report['keys']={'audited_contexts':len(contexts),'conflicts':conflicts,'death':'Ctrl+Alt+Shift+F10','gender':'Ctrl+Alt+Shift+F7'}

path='/Game/Developers/PlayerRagdoll/BP_PlayerRagdollTestGameMode'
if unreal.EditorAssetLibrary.does_asset_exist(path):bp=unreal.load_asset(path)
else:
    factory=unreal.BlueprintFactory();factory.set_editor_property('parent_class',unreal.SWCorpsePlaygroundMode)
    bp=unreal.AssetToolsHelpers.get_asset_tools().create_asset('BP_PlayerRagdollTestGameMode','/Game/Developers/PlayerRagdoll',unreal.Blueprint,factory)
cdo=unreal.get_default_object(bp.generated_class())
cdo.set_editor_property('player_controller_class',unreal.load_asset('/Game/Blueprints/Player/BP_BasePlayerController').generated_class())
cdo.set_editor_property('player_state_class',unreal.load_class(None,'/Script/ClassFeature.BasePlayerState'))
cdo.set_editor_property('default_pawn_class',unreal.load_asset('/Game/Blueprints/Player/BP_Player_Man').generated_class())
assert unreal.EditorAssetLibrary.save_loaded_asset(bp,False)
world=unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
world.get_world_settings().set_editor_property('default_game_mode',bp.generated_class())
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
cube=unreal.load_asset('/Engine/BasicShapes/Cube');material=unreal.load_asset('/Engine/BasicShapes/BasicShapeMaterial')
for location,scale in [(unreal.Vector(0,0,950),unreal.Vector(100,100,1)),(unreal.Vector(600,150,1010),unreal.Vector(3,3,.2)),(unreal.Vector(900,150,1020),unreal.Vector(3,3,.4)),(unreal.Vector(1200,150,1030),unreal.Vector(3,3,.6))]:
    actor=actors.spawn_actor_from_class(unreal.StaticMeshActor,location);component=actor.get_component_by_class(unreal.StaticMeshComponent)
    component.set_static_mesh(cube);component.set_material(0,material);component.set_collision_profile_name('BlockAll');actor.set_actor_scale3d(scale)
actors.spawn_actor_from_class(unreal.PlayerStart,unreal.Vector(0,0,1094),unreal.Rotator(0,0,0))
for rotation,intensity in [(unreal.Rotator(-50,-25,0),3.5),(unreal.Rotator(-35,150,0),2.)]:
    actor=actors.spawn_actor_from_class(unreal.DirectionalLight,unreal.Vector(0,0,1500),rotation);actor.get_component_by_class(unreal.DirectionalLightComponent).set_intensity(intensity)
sky=actors.spawn_actor_from_class(unreal.SkyLight,unreal.Vector(0,0,1600));light=sky.get_component_by_class(unreal.SkyLightComponent)
light.set_editor_property('source_type',unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP)
light.set_editor_property('cubemap',unreal.load_asset('/Engine/MapTemplates/Sky/DaylightAmbientCubemap'));light.set_intensity(1.)
map_path='/Game/Developers/PlayerRagdoll/L_PlayerRagdollTest'
assert unreal.EditorLoadingAndSavingUtils.save_map(world,map_path)
report['map']=map_path;report['passed']=True
(OUT/'playground_setup.json').write_text(json.dumps(report,indent=2),encoding='utf8')
unreal.SystemLibrary.quit_editor()
