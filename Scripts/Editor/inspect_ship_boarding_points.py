import unreal


def path_of(value):
    return value.get_path_name() if value else "None"


def inspect_actor(label, actor):
    unreal.log_warning(f"BOARDING_ACTOR label={label} actor={path_of(actor)}")
    if not actor:
        return
    for component in actor.get_components_by_class(unreal.ActorComponent):
        class_name = component.get_class().get_name()
        name = component.get_name()
        if isinstance(component, unreal.StaticMeshComponent):
            unreal.log_warning(
                f"BOARDING_COMPONENT owner={label} name={name} class={class_name} "
                f"mesh={path_of(component.get_editor_property('static_mesh'))} "
                f"visible={component.get_editor_property('visible')} "
                f"hidden_in_game={component.get_editor_property('hidden_in_game')}"
            )
        elif isinstance(component, unreal.ChildActorComponent):
            child_class = component.get_editor_property("child_actor_class")
            child_template = component.get_editor_property("child_actor_template")
            unreal.log_warning(
                f"BOARDING_CHILD owner={label} name={name} "
                f"class={path_of(child_class)} template={path_of(child_template)}"
            )
            if child_template:
                try:
                    mesh_asset = child_template.get_editor_property("point_mesh_asset")
                    unreal.log_warning(
                        f"BOARDING_CHILD_TEMPLATE name={name} point_mesh_asset={path_of(mesh_asset)}"
                    )
                except Exception as exc:
                    unreal.log_warning(f"BOARDING_CHILD_TEMPLATE name={name} no-point-property={exc}")


boarding_bp = unreal.EditorAssetLibrary.load_blueprint_class(
    "/Game/Blueprints/Ship/Blueprints/BP_ShipBoardingPoint"
)
boarding_cdo = unreal.get_default_object(boarding_bp) if boarding_bp else None
inspect_actor("BP_ShipBoardingPoint_CDO", boarding_cdo)
if boarding_cdo:
    unreal.log_warning(
        "BOARDING_DEFAULT "
        f"point_mesh_asset={path_of(boarding_cdo.get_editor_property('point_mesh_asset'))} "
        f"mesh_relative_transform={boarding_cdo.get_editor_property('mesh_relative_transform')}"
    )

for asset_path in (
    "/Game/Blueprints/Ship/Blueprints/BP_PlayerShip_Kelvin",
    "/Game/Blueprints/Ship/Blueprints/BP_PlayerShip",
    "/Game/Tests/Landscape/Kelvin/BP_PlayerShip_Kelvin",
):
    bp_class = unreal.EditorAssetLibrary.load_blueprint_class(asset_path)
    cdo = unreal.get_default_object(bp_class) if bp_class else None
    inspect_actor(asset_path, cdo)
    blueprint = unreal.EditorAssetLibrary.load_asset(asset_path)
    if blueprint:
        for outer in (blueprint, bp_class):
            if not outer:
                continue
            for obj in unreal.get_objects_with_outer(outer, include_nested_objects=True):
                if isinstance(obj, unreal.ChildActorComponent):
                    unreal.log_warning(
                        f"BOARDING_NESTED_CHILD asset={asset_path} object={path_of(obj)} "
                        f"class={path_of(obj.get_editor_property('child_actor_class'))} "
                        f"template={path_of(obj.get_editor_property('child_actor_template'))}"
                    )
                try:
                    mesh_asset = obj.get_editor_property("point_mesh_asset")
                    unreal.log_warning(
                        f"BOARDING_NESTED_POINT asset={asset_path} object={path_of(obj)} "
                        f"class={obj.get_class().get_name()} point_mesh_asset={path_of(mesh_asset)}"
                    )
                except Exception:
                    pass
