import unreal

material = unreal.load_asset("/Game/New/Water/Realistic_Water/M_Realistic_Water")
prefix = "/Game/New/Water/Realistic_Water/M_Realistic_Water.M_Realistic_Water:"

call_code = """#include "/Project/SWFluxOceanFoam.ush"
float OutFoam = 0.0;
SW_CALCULATE_FLUX_DEEP_OCEAN_FOAM(
    UV,
    Time,
    FoamHeight, FoamHeightSampler,
    FoamORM, FoamORMSampler,
    OutFoam);
return OutFoam;"""

mel = unreal.MaterialEditingLibrary

for cn_id in [15, 17]:
    cn = unreal.load_object(None, f"{prefix}MaterialExpressionCustom_{cn_id}")
    if cn:
        cn.set_editor_property("code", call_code)
        # also set include file paths if supported
        paths = list(cn.get_editor_property("include_file_paths"))
        if "/Project/SWFluxOceanFoam.ush" not in paths:
            paths.append("/Project/SWFluxOceanFoam.ush")
            cn.set_editor_property("include_file_paths", paths)
        unreal.log_warning(f"Updated Custom_{cn_id} to call SWFluxOceanFoam.ush!")

mel.recompile_material(material)
unreal.EditorAssetLibrary.save_loaded_asset(material)
unreal.log_warning("M_Realistic_Water recompiled and saved successfully!")
