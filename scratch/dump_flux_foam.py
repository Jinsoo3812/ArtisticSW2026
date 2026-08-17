import unreal

material = unreal.load_asset("/Game/New/Water/Realistic_Water/M_Realistic_Water")
prefix = "/Game/New/Water/Realistic_Water/M_Realistic_Water.M_Realistic_Water:"

custom_15 = unreal.load_object(None, f"{prefix}MaterialExpressionCustom_15")
custom_17 = unreal.load_object(None, f"{prefix}MaterialExpressionCustom_17")

if custom_15:
    unreal.log_warning("=== Custom_15 (MF_FluxDeepOceanFoam) FULL CODE ===")
    unreal.log_warning(custom_15.get_editor_property("code"))

if custom_17:
    unreal.log_warning("=== Custom_17 (MF_FluxDeepOceanFoam) FULL CODE ===")
    unreal.log_warning(custom_17.get_editor_property("code"))
