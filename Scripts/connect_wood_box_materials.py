import unreal


ROOT = "/Game/Fab/Wood_Box_Pack"


def load(name):
    asset = unreal.EditorAssetLibrary.load_asset(f"{ROOT}/{name}")
    if not asset:
        raise RuntimeError(f"Missing asset: {ROOT}/{name}")
    return asset


def configure_texture(texture, kind):
    texture.modify()
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
    unreal.EditorAssetLibrary.save_loaded_asset(texture, False)


def add_texture_parameter(material, name, texture, x, y, sampler_type):
    node = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionTextureSampleParameter2D, x, y
    )
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("texture", texture)
    node.set_editor_property("sampler_type", sampler_type)
    return node


master_path = f"{ROOT}/M_WoodBox_Master"
master = (
    unreal.EditorAssetLibrary.load_asset(master_path)
    if unreal.EditorAssetLibrary.does_asset_exist(master_path)
    else None
)
if not master:
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    master = tools.create_asset(
        "M_WoodBox_Master", ROOT, unreal.Material, unreal.MaterialFactoryNew()
    )
if not master:
    raise RuntimeError("Could not create M_WoodBox_Master")

master.modify()
master.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
master.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
unreal.MaterialEditingLibrary.delete_all_material_expressions(master)
base_node = add_texture_parameter(
    master, "BaseColor", load("T_Box_Wood_1_BaseColor"),
    -700, -250, unreal.MaterialSamplerType.SAMPLERTYPE_COLOR
)
normal_node = add_texture_parameter(
    master, "Normal", load("T_Box_Wood_1_Normal"),
    -700, 50, unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL
)
orm_node = add_texture_parameter(
    master, "ORM", load("T_Box_Wood_1_ORM"),
    -700, 350, unreal.MaterialSamplerType.SAMPLERTYPE_MASKS
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
    texture_prefix = "T_Box_wood_4" if index == 4 else f"T_Box_Wood_{index}"
    base_color = load(f"{texture_prefix}_BaseColor")
    normal = load(f"{texture_prefix}_Normal")
    orm = load(f"{texture_prefix}_ORM")

    configure_texture(base_color, "base_color")
    configure_texture(normal, "normal")
    configure_texture(orm, "orm")

    material.modify()
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

for mesh_name, index in (
    *((f"SM_WoodBox_{n}", 3) for n in range(1, 5)),
    *((f"SM_WoodBox_{n}", 2) for n in range(5, 7)),
    *((f"SM_WoodBox_{n}", 1) for n in range(7, 9)),
    *((f"SM_WoodBox_{n}", 4) for n in range(9, 11)),
    *((f"SM_CardboardBox_{n}", 4) for n in range(1, 5)),
    ("SM_Coil", 4),
):
    mesh = load(mesh_name)
    material = load(f"M_Box_wood_{index}")
    slots = mesh.get_editor_property("static_materials")
    if len(slots) != 1:
        raise RuntimeError(f"Expected one material slot on {mesh_name}, got {len(slots)}")
    if slots[0].material_interface != material:
        mesh.modify()
        mesh.set_material(0, material)
        unreal.EditorAssetLibrary.save_loaded_asset(mesh, False)
    unreal.log(f"Connected Wood Box mesh {mesh_name} to material {index}")

unreal.log("WOOD_BOX_MATERIAL_SETUP_COMPLETE")
