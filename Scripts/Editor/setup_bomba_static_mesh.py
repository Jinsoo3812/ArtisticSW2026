import unreal

SOURCE_FBX = r"C:\ProgramData\Epic\EpicGamesLauncher\VaultCache\FabLibrary\Bomb__cannonball_low_poly-42ccc761\fbx\bomb-cannonball-low-poly_extracted\source\Bomba.fbx"
ROOT = "/Game/Blueprints/Ship/Enemy_Ship/Torpeodo"
TEXTURES = ROOT + "/textures"
MATERIAL_PATH = ROOT + "/M_Bomba"
MESH_PATH = ROOT + "/SM_Bomba"

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()

base_color = unreal.EditorAssetLibrary.load_asset(TEXTURES + "/Bomba_Base_Color")
metallic = unreal.EditorAssetLibrary.load_asset(TEXTURES + "/Bomba_Metallic")
ao = unreal.EditorAssetLibrary.load_asset(TEXTURES + "/Bomba_Mixed_AO")
normal = unreal.EditorAssetLibrary.load_asset(TEXTURES + "/Bomba_Normal_OpenGL")
if not all((base_color, metallic, ao, normal)):
    raise RuntimeError("One or more Bomba textures are missing")

base_color.set_editor_property("srgb", True)
for texture in (metallic, ao, normal):
    texture.set_editor_property("srgb", False)
normal.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
normal.set_editor_property("flip_green_channel", True)
for texture in (base_color, metallic, ao, normal):
    unreal.EditorAssetLibrary.save_loaded_asset(texture)

material = unreal.EditorAssetLibrary.load_asset(MATERIAL_PATH)
if not material:
    material = asset_tools.create_asset("M_Bomba", ROOT, unreal.Material, unreal.MaterialFactoryNew())
if not material:
    raise RuntimeError("Could not create M_Bomba")
material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
unreal.MaterialEditingLibrary.delete_all_material_expressions(material)

def texture_sample(texture, x, y):
    node = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionTextureSample, x, y)
    node.set_editor_property("texture", texture)
    return node

base_node = texture_sample(base_color, -500, -300)
normal_node = texture_sample(normal, -500, 0)
metallic_node = texture_sample(metallic, -500, 260)
ao_node = texture_sample(ao, -500, 480)
roughness = unreal.MaterialEditingLibrary.create_material_expression(
    material, unreal.MaterialExpressionScalarParameter, -250, 380)
roughness.set_editor_property("parameter_name", "Roughness")
roughness.set_editor_property("default_value", 0.55)

unreal.MaterialEditingLibrary.connect_material_property(base_node, "RGB", unreal.MaterialProperty.MP_BASE_COLOR)
unreal.MaterialEditingLibrary.connect_material_property(normal_node, "RGB", unreal.MaterialProperty.MP_NORMAL)
unreal.MaterialEditingLibrary.connect_material_property(metallic_node, "R", unreal.MaterialProperty.MP_METALLIC)
unreal.MaterialEditingLibrary.connect_material_property(ao_node, "R", unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)
unreal.MaterialEditingLibrary.connect_material_property(roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)
unreal.MaterialEditingLibrary.recompile_material(material)
unreal.EditorAssetLibrary.save_loaded_asset(material)

mesh = unreal.EditorAssetLibrary.load_asset(MESH_PATH)
if not mesh:
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", SOURCE_FBX)
    task.set_editor_property("destination_path", ROOT)
    task.set_editor_property("destination_name", "SM_Bomba")
    task.set_editor_property("automated", True)
    task.set_editor_property("save", True)
    task.set_editor_property("replace_existing", False)
    options = unreal.FbxImportUI()
    options.set_editor_property("import_mesh", True)
    options.set_editor_property("import_materials", False)
    options.set_editor_property("import_textures", False)
    options.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_STATIC_MESH)
    options.static_mesh_import_data.set_editor_property("combine_meshes", True)
    options.static_mesh_import_data.set_editor_property("generate_lightmap_u_vs", True)
    options.static_mesh_import_data.set_editor_property("auto_generate_collision", True)
    task.set_editor_property("options", options)
    asset_tools.import_asset_tasks([task])
    mesh = unreal.EditorAssetLibrary.load_asset(MESH_PATH)
if not mesh:
    raise RuntimeError("Could not import combined SM_Bomba")

slot_count = max(1, len(mesh.get_editor_property("static_materials")))
for index in range(slot_count):
    mesh.set_material(index, material)
unreal.EditorAssetLibrary.save_loaded_asset(mesh)
unreal.log_warning("BOMBA_SETUP_COMPLETE mesh=" + mesh.get_path_name() + " material=" + material.get_path_name())
