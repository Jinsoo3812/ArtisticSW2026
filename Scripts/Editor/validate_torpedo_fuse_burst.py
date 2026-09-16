import unreal

bp = unreal.EditorAssetLibrary.load_asset("/Game/Blueprints/Ship/Enemy_Ship/Blueprints/BP_ES_Torpedo")
mesh = unreal.EditorAssetLibrary.load_asset("/Game/Blueprints/Ship/Enemy_Ship/Torpeodo/SM_Bomba")
system = unreal.EditorAssetLibrary.load_asset("/Game/Pack_Simple_Particle_Burst/01_Niagara_Systems/NS_Simple_Burst_Level_1")
errors = []
if not bp or not mesh or not system:
    errors.append("required asset missing")
else:
    cdo = unreal.get_default_object(bp.generated_class())
    if cdo.get_editor_property("fuse_burst_system") != system:
        errors.append("FuseBurstSystem mismatch")
    if str(cdo.get_editor_property("fuse_socket_name")) != "FuseTip":
        errors.append("Fuse socket mismatch")
    if abs(cdo.get_editor_property("fuse_burst_interval_seconds") - 0.3) > 0.001:
        errors.append("Fuse interval mismatch")
    if abs(cdo.get_editor_property("fuse_burst_scale") - 0.25) > 0.001:
        errors.append("Fuse scale mismatch")
    mesh_component = next((c for c in cdo.get_components_by_class(unreal.StaticMeshComponent) if c.get_name() == "CannonballMesh"), None)
    if not mesh_component or mesh_component.get_editor_property("static_mesh") != mesh:
        errors.append("CannonballMesh is not SM_Bomba")
    niagara = next((c for c in cdo.get_components_by_class(unreal.NiagaraComponent) if c.get_name() == "FuseBurstComponent"), None)
    if not niagara:
        errors.append("FuseBurstComponent missing")
    else:
        if str(niagara.get_attach_socket_name()) != "FuseTip":
            errors.append("Niagara attachment socket mismatch")
        if niagara.get_editor_property("auto_activate"):
            errors.append("Niagara AutoActivate should be false; timer owns playback")

if errors:
    for error in errors:
        unreal.log_error("TORPEDO_FUSE_VALIDATE " + error)
    raise RuntimeError("Torpedo fuse validation failed")
unreal.log_warning("TORPEDO_FUSE_VALIDATE_OK")
