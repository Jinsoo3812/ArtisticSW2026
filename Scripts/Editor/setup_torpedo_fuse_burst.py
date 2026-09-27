import unreal

bp_path = "/Game/Blueprints/Ship/Enemy_Ship/Blueprints/BP_ES_Torpedo"
mesh_path = "/Game/Blueprints/Ship/Enemy_Ship/Torpeodo/SM_Bomba"
system_path = "/Game/Pack_Simple_Particle_Burst/01_Niagara_Systems/NS_Simple_Burst_Level_1"

bp = unreal.EditorAssetLibrary.load_asset(bp_path)
mesh = unreal.EditorAssetLibrary.load_asset(mesh_path)
system = unreal.EditorAssetLibrary.load_asset(system_path)
if not bp or not mesh or not system:
    raise RuntimeError("Required torpedo fuse assets are missing")

cdo = unreal.get_default_object(bp.generated_class())
cdo.set_editor_property("fuse_burst_system", system)
cdo.set_editor_property("fuse_socket_name", "FuseTip")
cdo.set_editor_property("fuse_burst_interval_seconds", 0.3)
cdo.set_editor_property("fuse_burst_scale", 0.25)

mesh_components = cdo.get_components_by_class(unreal.StaticMeshComponent)
cannonball_mesh = next((component for component in mesh_components if component.get_name() == "CannonballMesh"), None)
if not cannonball_mesh:
    raise RuntimeError("BP_ES_Torpedo CannonballMesh component is missing")
cannonball_mesh.set_static_mesh(mesh)

unreal.EditorAssetLibrary.save_loaded_asset(bp)
unreal.log_warning("TORPEDO_FUSE_SETUP_COMPLETE")
