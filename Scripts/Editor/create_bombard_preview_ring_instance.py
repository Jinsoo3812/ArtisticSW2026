import unreal

FOLDER = "/Game/Blueprints/Ship/Skill/Bombardment"
MASTER_PATH = FOLDER + "/M_BombardPreview"
INSTANCE_PATH = FOLDER + "/MI_BombardPreview"
BLUEPRINT_PATH = FOLDER + "/BP_BombardmentPreview"

master = unreal.EditorAssetLibrary.load_asset(MASTER_PATH)
if not master:
    raise RuntimeError("Could not load " + MASTER_PATH)

instance = unreal.EditorAssetLibrary.load_asset(INSTANCE_PATH)
if not instance:
    instance = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        "MI_BombardPreview", FOLDER, unreal.MaterialInstanceConstant,
        unreal.MaterialInstanceConstantFactoryNew())
if not instance:
    raise RuntimeError("Could not create " + INSTANCE_PATH)

unreal.MaterialEditingLibrary.set_material_instance_parent(instance, master)
unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(
    instance, "InnerRadius", 0.42)
unreal.EditorAssetLibrary.save_loaded_asset(instance, only_if_is_dirty=False)

generated_class = unreal.EditorAssetLibrary.load_blueprint_class(BLUEPRINT_PATH)
if not generated_class:
    raise RuntimeError("Could not load generated class for " + BLUEPRINT_PATH)
cdo = unreal.get_default_object(generated_class)
cdo.set_editor_property("preview_material", instance)
blueprint = unreal.EditorAssetLibrary.load_asset(BLUEPRINT_PATH)
unreal.EditorAssetLibrary.save_loaded_asset(blueprint, only_if_is_dirty=False)
unreal.log_warning("BOMBARD_PREVIEW_MI_ASSIGNED InnerRadius=0.42")
