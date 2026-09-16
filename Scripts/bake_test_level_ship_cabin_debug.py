import unreal


LEVEL = "/Game/Level/Test_Level"


def main():
    unreal.EditorLoadingAndSavingUtils.load_map(LEVEL)
    result = unreal.ShipCabinVolumeBakerLibrary.bake_tagged_cabin_debug(
        voxel_size=10.0,
        surface_thickness=12.0,
        bounds_padding=30.0,
    )
    unreal.log_warning(
        "SW_CABIN_BAKE success={} leaked={} filled={} instances={} message={}".format(
            result.success,
            result.leaked_to_exterior,
            result.filled_voxel_count,
            result.debug_instance_count,
            result.message,
        )
    )
    if result.success:
        if not unreal.EditorAssetLibrary.save_directory(
                "/Game/Blueprints/Water/Culling", only_if_is_dirty=False, recursive=True):
            raise RuntimeError("Cabin bake succeeded but runtime culling assets could not be saved")
        if not unreal.EditorLoadingAndSavingUtils.save_current_level():
            raise RuntimeError("Cabin bake succeeded but Test_Level could not be saved")
    else:
        raise RuntimeError(result.message)


main()
