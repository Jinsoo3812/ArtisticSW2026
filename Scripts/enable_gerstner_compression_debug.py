"""Enable the white Gerstner Jacobian debug permutation on the Ocean MI."""
import unreal


INSTANCE_PATH = "/Game/Blueprints/Water/M_Realistic_Water_Ocean"
PARAMETER = "SW Gerstner Compression Debug"

instance = unreal.load_asset(INSTANCE_PATH)
if instance is None:
    raise RuntimeError("Could not load " + INSTANCE_PATH)

# UE 5.7's Python wrapper currently returns false even though the editor-only
# setter applies the value, so verify by saving/reloading in the validator.
unreal.MaterialEditingLibrary.set_material_instance_static_switch_parameter_value(
    instance, PARAMETER, True)

unreal.EditorAssetLibrary.save_loaded_asset(instance, only_if_is_dirty=False)
unreal.log("SW_GERSTNER_COMPRESSION_DEBUG_OCEAN_MI=ON")
