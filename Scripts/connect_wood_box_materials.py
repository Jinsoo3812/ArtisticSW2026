import unreal


ROOT = "/Game/Wood_Box"


def load(name):
    asset = unreal.EditorAssetLibrary.load_asset(f"{ROOT}/{name}")
    if not asset:
        raise RuntimeError(f"Missing asset: {ROOT}/{name}")
    return asset


def configure_texture(texture, kind):
    if kind == "base_color":
        texture.set_editor_property("srgb", True)
    elif kind == "normal":
        texture.set_editor_property("srgb", False)
        texture.set_editor_property(
            "compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP
        )
    elif kind == "orm":
        texture.set_editor_property("srgb", False)
        texture.set_editor_property(
            "compression_settings", unreal.TextureCompressionSettings.TC_MASKS
        )
    texture.modify()
    unreal.EditorAssetLibrary.save_loaded_asset(texture, False)


def add_texture_parameter(material, name, x, y, sampler_type):
    node = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionTextureSampleParameter2D, x, y
    )
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("sampler_type", sampler_type)
    return node


master_path = f"{ROOT}/M_WoodBox_Master"
master = unreal.EditorAssetLibrary.load_asset(master_path)
if not master:
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    master = tools.create_asset(
        "M_WoodBox_Master", ROOT, unreal.Material, unreal.MaterialFactoryNew()
    )
if not master:
    raise RuntimeError("Could not create M_WoodBox_Master")

unreal.MaterialEditingLibrary.delete_all_material_expressions(master)
base_node = add_texture_parameter(
    master, "BaseColor", -700, -250, unreal.MaterialSamplerType.SAMPLERTYPE_COLOR
)
normal_node = add_texture_parameter(
    master, "Normal", -700, 50, unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL
)
orm_node = add_texture_parameter(
    master, "ORM", -700, 350, unreal.MaterialSamplerType.SAMPLERTYPE_MASKS
)
unreal.MaterialEditingLibrary.connect_material_property(
    base_node, "RGB", unreal.MaterialProperty.MP_BASE_COLOR
)
unreal.MaterialEditingLibrary.connect_material_property(
    normal_node, "RGB", unreal.MaterialProperty.MP_NORMAL
)
unreal.MaterialEditingLibrary.connect_material_property(
    orm_node, "R", unreal.MaterialProperty.MP_AMBIENT_OCCLUSION
)
unreal.MaterialEditingLibrary.connect_material_property(
    orm_node, "G", unreal.MaterialProperty.MP_ROUGHNESS
)
unreal.MaterialEditingLibrary.connect_material_property(
    orm_node, "B", unreal.MaterialProperty.MP_METALLIC
)
unreal.MaterialEditingLibrary.recompile_material(master)
unreal.EditorAssetLibrary.save_loaded_asset(master, False)


for index in range(1, 5):
    material = load(f"M_Box_wood_{index}")
    base_color = load(f"T_Box_Wood_{index}_BaseColor")
    normal = load(f"T_Box_Wood_{index}_Normal")
    orm = load(f"T_Box_Wood_{index}_ORM")

    configure_texture(base_color, "base_color")
    configure_texture(normal, "normal")
    configure_texture(orm, "orm")

    unreal.MaterialEditingLibrary.set_material_instance_parent(material, master)
    unreal.MaterialEditingLibrary.set_material_instance_texture_parameter_value(
        material, "BaseColor", base_color
    )
    unreal.MaterialEditingLibrary.set_material_instance_texture_parameter_value(
        material, "Normal", normal
    )
    unreal.MaterialEditingLibrary.set_material_instance_texture_parameter_value(
        material, "ORM", orm
    )
    unreal.EditorAssetLibrary.save_loaded_asset(material, False)
    unreal.log(f"Connected Wood Box material {index}")

unreal.log("WOOD_BOX_MATERIAL_SETUP_COMPLETE")
