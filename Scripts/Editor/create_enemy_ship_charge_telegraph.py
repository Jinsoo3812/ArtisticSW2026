import unreal


ROOT = "/Game/Blueprints/Ship/Enemy_Ship/Telegraph"
SOURCE_PNG = r"C:/Unreal Projects/ArtisticSW2026/Content/Blueprints/Ship/Enemy_Ship/Telegraph/Source/T_ES_ChargeArrow_Test.png"
TEXTURE_PATH = ROOT + "/T_ES_ChargeArrow_Test"
MESH_PATH = ROOT + "/SM_ES_ChargeTelegraphPlane"
MATERIAL_PATH = ROOT + "/M_ES_ChargeTelegraph"
ACTOR_BP_PATH = ROOT + "/BP_ES_ChargeTelegraph"
ABILITY_BP_PATH = "/Game/Blueprints/Ship/Enemy_Ship/GA/BP_GA_ES_Charge"


def delete_if_present(asset_path):
    if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        unreal.EditorAssetLibrary.delete_asset(asset_path)


def import_texture():
    existing = unreal.EditorAssetLibrary.load_asset(TEXTURE_PATH)
    if existing:
        texture = existing
        texture.set_editor_property("srgb", False)
        texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
        texture.set_editor_property("address_x", unreal.TextureAddress.TA_WRAP)
        texture.set_editor_property("address_y", unreal.TextureAddress.TA_CLAMP)
        texture.modify()
        unreal.EditorAssetLibrary.save_loaded_asset(texture)
        return texture
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", SOURCE_PNG)
    task.set_editor_property("destination_path", ROOT)
    task.set_editor_property("destination_name", "T_ES_ChargeArrow_Test")
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", True)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    texture = unreal.EditorAssetLibrary.load_asset(TEXTURE_PATH)
    if not texture:
        raise RuntimeError("Failed to import test charge texture")
    texture.set_editor_property("srgb", False)
    texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
    texture.set_editor_property("address_x", unreal.TextureAddress.TA_WRAP)
    texture.set_editor_property("address_y", unreal.TextureAddress.TA_CLAMP)
    texture.modify()
    unreal.EditorAssetLibrary.save_loaded_asset(texture)
    return texture


def copy_plane():
    delete_if_present(MESH_PATH)
    if not unreal.EditorAssetLibrary.duplicate_asset("/Engine/BasicShapes/Plane.Plane", MESH_PATH):
        raise RuntimeError("Failed to copy Engine Plane")
    mesh = unreal.EditorAssetLibrary.load_asset(MESH_PATH)
    unreal.EditorAssetLibrary.save_loaded_asset(mesh)
    return mesh


def expression(material, cls, x, y):
    return unreal.MaterialEditingLibrary.create_material_expression(material, cls, x, y)


def scalar_parameter(material, name, value, x, y):
    node = expression(material, unreal.MaterialExpressionScalarParameter, x, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("default_value", value)
    return node


def vector_parameter(material, name, value, x, y):
    node = expression(material, unreal.MaterialExpressionVectorParameter, x, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("default_value", value)
    return node


def connect(source, source_output, target, target_input):
    unreal.MaterialEditingLibrary.connect_material_expressions(
        source, source_output, target, target_input)


def create_material(texture):
    delete_if_present(MATERIAL_PATH)
    factory = unreal.MaterialFactoryNew()
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        "M_ES_ChargeTelegraph", ROOT, unreal.Material, factory)
    if not material:
        raise RuntimeError("Failed to create charge telegraph material")
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("two_sided", True)

    texcoord = expression(material, unreal.MaterialExpressionTextureCoordinate, -1200, 0)
    mask_u = expression(material, unreal.MaterialExpressionComponentMask, -1000, -100)
    mask_u.set_editor_property("r", True)
    mask_v = expression(material, unreal.MaterialExpressionComponentMask, -1000, 140)
    mask_v.set_editor_property("g", True)
    connect(texcoord, "", mask_u, "Input")
    connect(texcoord, "", mask_v, "Input")

    repeat = scalar_parameter(material, "ArrowRepeatCount", 12.5, -1000, -300)
    repeat_u = expression(material, unreal.MaterialExpressionMultiply, -760, -120)
    connect(mask_u, "", repeat_u, "A")
    connect(repeat, "", repeat_u, "B")

    time = expression(material, unreal.MaterialExpressionTime, -1000, -520)
    speed = scalar_parameter(material, "ArrowScrollSpeed", -0.8, -1000, -650)
    scroll = expression(material, unreal.MaterialExpressionMultiply, -760, -500)
    connect(time, "", scroll, "A")
    connect(speed, "", scroll, "B")
    scrolling_u = expression(material, unreal.MaterialExpressionAdd, -520, -120)
    connect(repeat_u, "", scrolling_u, "A")
    connect(scroll, "", scrolling_u, "B")
    uv = expression(material, unreal.MaterialExpressionAppendVector, -300, 0)
    connect(scrolling_u, "", uv, "A")
    connect(mask_v, "", uv, "B")

    sample = expression(material, unreal.MaterialExpressionTextureSampleParameter2D, -80, 0)
    sample.set_editor_property("parameter_name", "ArrowMask")
    sample.set_editor_property("texture", texture)
    connect(uv, "", sample, "Coordinates")

    base_color = vector_parameter(
        material, "BaseColor", unreal.LinearColor(1.0, 0.0, 0.0, 1.0), 180, -260)
    arrow_color = vector_parameter(
        material, "ArrowColor", unreal.LinearColor(1.0, 0.75, 0.05, 1.0), 180, -100)
    color_lerp = expression(material, unreal.MaterialExpressionLinearInterpolate, 440, -100)
    connect(base_color, "", color_lerp, "A")
    connect(arrow_color, "", color_lerp, "B")
    connect(sample, "A", color_lerp, "Alpha")
    unreal.MaterialEditingLibrary.connect_material_property(
        color_lerp, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    base_opacity = scalar_parameter(material, "BaseOpacity", 0.25, 180, 180)
    arrow_opacity = scalar_parameter(material, "ArrowOpacity", 0.85, 180, 330)
    opacity_lerp = expression(material, unreal.MaterialExpressionLinearInterpolate, 440, 240)
    connect(base_opacity, "", opacity_lerp, "A")
    connect(arrow_opacity, "", opacity_lerp, "B")
    connect(sample, "A", opacity_lerp, "Alpha")
    unreal.MaterialEditingLibrary.connect_material_property(
        opacity_lerp, "", unreal.MaterialProperty.MP_OPACITY)

    unreal.MaterialEditingLibrary.recompile_material(material)
    unreal.EditorAssetLibrary.save_loaded_asset(material)
    return material


def create_actor_blueprint(mesh, material):
    delete_if_present(ACTOR_BP_PATH)
    parent = unreal.load_class(None, "/Script/Enemy.EnemyShipChargeTelegraph")
    if not parent:
        raise RuntimeError("Native EnemyShipChargeTelegraph class is unavailable")
    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", parent)
    blueprint = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        "BP_ES_ChargeTelegraph", ROOT, unreal.Blueprint, factory)
    unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
    generated_class = unreal.EditorAssetLibrary.load_blueprint_class(ACTOR_BP_PATH)
    cdo = unreal.get_default_object(generated_class)
    cdo.set_editor_property("warning_material", material)
    cdo.set_editor_property("symbol_spacing", 800.0)
    plane = cdo.get_editor_property("warning_plane")
    plane.set_static_mesh(mesh)
    blueprint.modify()
    unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
    unreal.EditorAssetLibrary.save_loaded_asset(blueprint)
    return generated_class


def assign_to_charge_ability(telegraph_class):
    blueprint = unreal.EditorAssetLibrary.load_asset(ABILITY_BP_PATH)
    if not blueprint:
        raise RuntimeError("BP_GA_ES_Charge was not found")
    generated_class = unreal.EditorAssetLibrary.load_blueprint_class(ABILITY_BP_PATH)
    cdo = unreal.get_default_object(generated_class)
    cdo.set_editor_property("charge_distance", 10000.0)
    cdo.set_editor_property("charge_propulsion_multiplier", 2.0)
    cdo.set_editor_property("charge_failsafe_duration_seconds", 0.0)
    cdo.set_editor_property("charge_telegraph_width", 1000.0)
    cdo.set_editor_property("charge_telegraph_world_z", 20.0)
    cdo.set_editor_property("charge_telegraph_class", telegraph_class)
    blueprint.modify()
    unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
    unreal.EditorAssetLibrary.save_loaded_asset(blueprint)


texture = import_texture()
mesh = copy_plane()
material = create_material(texture)
telegraph_class = create_actor_blueprint(mesh, material)
assign_to_charge_ability(telegraph_class)
unreal.EditorAssetLibrary.save_directory(ROOT, only_if_is_dirty=False, recursive=True)
unreal.log("CODEX_CHARGE_TELEGRAPH_CREATED")
