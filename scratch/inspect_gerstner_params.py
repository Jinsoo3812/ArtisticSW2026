import unreal

map_path = "/Game/New/Water/Realistic_Water/Realistic_Water"
world = unreal.load_asset(map_path)
unreal.log_warning("=== Checking Level Actors for Water Waves ===")

# In UE python, we can search actors in world if loaded or look at BP
# Let's inspect WaterBodyOcean asset or wave generator if any
wave_assets = unreal.AssetRegistryHelpers.get_asset_registry().get_assets_by_class("WaterWavesBase")
for a in wave_assets:
    unreal.log_warning(f"WaterWaves Asset: {a.package_name}")

# Also check Gerstner wave parameters in M_Realistic_Water
material = unreal.load_asset("/Game/New/Water/Realistic_Water/M_Realistic_Water")
prefix = "/Game/New/Water/Realistic_Water/M_Realistic_Water.M_Realistic_Water:"

for i in range(500):
    sp = unreal.load_object(None, f"{prefix}MaterialExpressionScalarParameter_{i}")
    if sp:
        pname = str(sp.get_editor_property("parameter_name"))
        val = sp.get_editor_property("default_value")
        if any(k in pname.lower() for k in ["gerstner", "wave", "height", "amp", "steep", "crest", "slope"]):
            unreal.log_warning(f"ScalarParam: '{pname}' = {val}")

