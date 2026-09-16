import unreal


ROOT = "/Game/Ladder"
MASTER_PATH = f"{ROOT}/M_Ladder_Master"
INSTANCE_PATH = f"{ROOT}/MatID_1"
MESH_PATH = f"{ROOT}/Wooden_Ladder_venqbcobw_Raw"
PREFIX = "Wooden_Ladder_venqbcobw_Raw_8K_"


def load(path):
    asset = unreal.EditorAssetLibrary.load_asset(path)
    if not asset:
        raise RuntimeError(f"Missing Ladder asset: {path}")
    return asset


def save(asset):
    asset.modify()
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset, False):
        raise RuntimeError(f"Failed to save: {asset.get_path_name()}")


def configure_texture(texture, srgb, compression):
    texture.set_editor_property("srgb", srgb)
    texture.set_editor_property("compression_settings", compression)
    save(texture)


def texture_parameter(material, parameter_name, texture, x, y, sampler_type):
    node = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionTextureSampleParameter2D, x, y
    )
    node.set_editor_property("parameter_name", parameter_name)
    node.set_editor_property("texture", texture)
    node.set_editor_property("sampler_type", sampler_type)
    return node


base_color = load(f"{ROOT}/{PREFIX}BaseColor")
normal = load(f"{ROOT}/{PREFIX}Normal")
roughness = load(f"{ROOT}/{PREFIX}Roughness")
specular = load(f"{ROOT}/{PREFIX}Specular")
ambient_occlusion = load(f"{ROOT}/{PREFIX}AO")
cavity = load(f"{ROOT}/{PREFIX}Cavity")

configure_texture(
    base_color, True, unreal.TextureCompressionSettings.TC_DEFAULT
)
configure_texture(
    normal, False, unreal.TextureCompressionSettings.TC_NORMALMAP
)
for mask in (roughness, specular, ambient_occlusion, cavity):
    configure_texture(mask, False, unreal.TextureCompressionSettings.TC_MASKS)

master = unreal.EditorAssetLibrary.load_asset(MASTER_PATH)
if not master:
    master = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        "M_Ladder_Master", ROOT, unreal.Material, unreal.MaterialFactoryNew()
    )
if not master:
    raise RuntimeError("Could not create M_Ladder_Master")

master.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
master.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
unreal.MaterialEditingLibrary.delete_all_material_expressions(master)

base_node = texture_parameter(
    master, "BaseColor", base_color, -800, -350,
    unreal.MaterialSamplerType.SAMPLERTYPE_COLOR
)
normal_node = texture_parameter(
    master, "Normal", normal, -800, -100,
    unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL
)
roughness_node = texture_parameter(
    master, "Roughness", roughness, -800, 150,
    unreal.MaterialSamplerType.SAMPLERTYPE_MASKS
)
specular_node = texture_parameter(
    master, "Specular", specular, -800, 350,
    unreal.MaterialSamplerType.SAMPLERTYPE_MASKS
)
ao_node = texture_parameter(
    master, "AmbientOcclusion", ambient_occlusion, -800, 550,
    unreal.MaterialSamplerType.SAMPLERTYPE_MASKS
)
cavity_node = texture_parameter(
    master, "Cavity", cavity, -800, 750,
    unreal.MaterialSamplerType.SAMPLERTYPE_MASKS
)
ao_cavity = unreal.MaterialEditingLibrary.create_material_expression(
    master, unreal.MaterialExpressionMultiply, -420, 620
)
metallic = unreal.MaterialEditingLibrary.create_material_expression(
    master, unreal.MaterialExpressionConstant, -420, 850
)
metallic.set_editor_property("r", 0.0)

unreal.MaterialEditingLibrary.connect_material_property(
    base_node, "RGB", unreal.MaterialProperty.MP_BASE_COLOR
)
unreal.MaterialEditingLibrary.connect_material_property(
    normal_node, "RGB", unreal.MaterialProperty.MP_NORMAL
)
unreal.MaterialEditingLibrary.connect_material_property(
    roughness_node, "R", unreal.MaterialProperty.MP_ROUGHNESS
)
unreal.MaterialEditingLibrary.connect_material_property(
    specular_node, "R", unreal.MaterialProperty.MP_SPECULAR
)
unreal.MaterialEditingLibrary.connect_material_expressions(ao_node, "R", ao_cavity, "A")
unreal.MaterialEditingLibrary.connect_material_expressions(cavity_node, "R", ao_cavity, "B")
unreal.MaterialEditingLibrary.connect_material_property(
    ao_cavity, "", unreal.MaterialProperty.MP_AMBIENT_OCCLUSION
)
unreal.MaterialEditingLibrary.connect_material_property(
    metallic, "", unreal.MaterialProperty.MP_METALLIC
)
unreal.MaterialEditingLibrary.recompile_material(master)
save(master)

instance = load(INSTANCE_PATH)
if not isinstance(instance, unreal.MaterialInstanceConstant):
    raise RuntimeError(f"Expected MaterialInstanceConstant: {INSTANCE_PATH}")
unreal.MaterialEditingLibrary.set_material_instance_parent(instance, master)
for parameter_name, texture in (
    ("BaseColor", base_color),
    ("Normal", normal),
    ("Roughness", roughness),
    ("Specular", specular),
    ("AmbientOcclusion", ambient_occlusion),
    ("Cavity", cavity),
):
    unreal.MaterialEditingLibrary.set_material_instance_texture_parameter_value(
        instance, parameter_name, texture
    )
save(instance)

mesh = load(MESH_PATH)
if not isinstance(mesh, unreal.StaticMesh):
    raise RuntimeError(f"Expected StaticMesh: {MESH_PATH}")
mesh.set_material(0, instance)
save(mesh)

unreal.log_warning(
    "LADDER_MATERIAL_SETUP_COMPLETE "
    f"mesh={mesh.get_path_name()} instance={instance.get_path_name()} "
    f"master={master.get_path_name()}"
)
