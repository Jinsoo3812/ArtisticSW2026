import unreal


ROOT = "/Game/Tests/Landscape/Kelvin"
SOURCE_MASTER = "/Game/Tests/Landscape/Shore_M_RealisticWater_GodotInspired"
SOURCE_INSTANCE = "/Game/Tests/Landscape/Shore_M_RealisticWater_GodotInspired_Ocean"
SOURCE_WAVES = "/Game/Tests/Landscape/Shore_Waves_RealisticWater"
SOURCE_SHIP = "/Game/New/Ship/Blueprints/BP_PlayerShip"

MASTER = f"{ROOT}/M_Kelvin_RealisticWater"
INSTANCE = f"{ROOT}/MI_Kelvin_RealisticWater_Ocean"
WAVES = f"{ROOT}/Kelvin_Waves_RealisticWater"
SHIP = f"{ROOT}/BP_PlayerShip_Kelvin"


def load(path):
    asset = unreal.load_asset(path)
    if not asset:
        raise RuntimeError(f"Required asset is missing: {path}")
    return asset


def duplicate_once(source, destination):
    existing = unreal.load_asset(destination)
    if existing:
        return existing, False
    if not unreal.EditorAssetLibrary.duplicate_asset(source, destination):
        raise RuntimeError(f"Failed to duplicate {source} -> {destination}")
    return load(destination), True


def new_custom_input(name):
    value = unreal.CustomInput()
    value.set_editor_property("input_name", name)
    return value


def set_desc(expression, value):
    try:
        expression.set_editor_property("desc", value)
    except Exception:
        pass


def create_scalar(material, name, default, x, y):
    node = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionScalarParameter, x, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("default_value", default)
    return node


def create_vector(material, name, default, x, y):
    node = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionVectorParameter, x, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("default_value", default)
    return node


def connect(source, source_output, target, target_input):
    if not unreal.MaterialEditingLibrary.connect_material_expressions(
            source, source_output, target, target_input):
        raise RuntimeError(
            f"Failed material connection: {source.get_name()}[{source_output}] -> "
            f"{target.get_name()}[{target_input}]")


unreal.EditorAssetLibrary.make_directory(ROOT)
master, master_created = duplicate_once(SOURCE_MASTER, MASTER)
instance, _ = duplicate_once(SOURCE_INSTANCE, INSTANCE)
waves, _ = duplicate_once(SOURCE_WAVES, WAVES)
ship, ship_created = duplicate_once(SOURCE_SHIP, SHIP)

if not master_created:
    raise RuntimeError(
        f"{MASTER} already exists. Refusing to append a second Kelvin graph. "
        "Delete only the generated Kelvin assets if a clean rebuild is intended.")

# Point the copied instance at the copied master. The source instance remains untouched.
unreal.MaterialEditingLibrary.set_material_instance_parent(instance, master)

# Reparent the copied player Blueprint so it inherits the native wake component.
if ship_created:
    unreal.BlueprintEditorLibrary.reparent_blueprint(ship, unreal.KelvinShip.static_class())
unreal.BlueprintEditorLibrary.compile_blueprint(ship)

# The source material uses Material Attributes. Inject WPO, roughness and emissive
# immediately before its final SetMaterialAttributes node, keeping the existing graph intact.
final_attributes = unreal.load_object(
    None,
    f"{MASTER}.M_Kelvin_RealisticWater:MaterialExpressionSetMaterialAttributes_2")
if not final_attributes:
    raise RuntimeError("Copied material final SetMaterialAttributes node was not found")

input_names = list(unreal.MaterialEditingLibrary.get_material_expression_input_names(final_attributes))
input_nodes = list(unreal.MaterialEditingLibrary.get_inputs_for_material_expression(master, final_attributes))
inputs_by_name = {str(name): input_nodes[index] for index, name in enumerate(input_names)}
for required in ("World Position Offset", "Roughness", "Emissive Color"):
    if required not in inputs_by_name or not inputs_by_name[required]:
        raise RuntimeError(f"Final material attributes input is missing: {required}")

world_position = unreal.MaterialEditingLibrary.create_material_expression(
    master, unreal.MaterialExpressionWorldPosition, 11800, -1500)
set_desc(world_position, "KELVIN M1: Absolute world position")

wake_texture = unreal.MaterialEditingLibrary.create_material_expression(
    master, unreal.MaterialExpressionTextureObjectParameter, 11800, -1250)
wake_texture.set_editor_property("parameter_name", "ShipWakeTex")
wake_texture.set_editor_property("texture", load("/Engine/EngineResources/DefaultTexture"))
set_desc(wake_texture, "KELVIN M1: runtime 64x3 event texture")

wake_time = create_scalar(master, "ShipWakeServerTime", 0.0, 11800, -1000)
wake_count = create_scalar(master, "ShipWakeCount", 0.0, 11800, -800)
wake_height_scale = create_scalar(master, "ShipWakeHeightScale", 1.0, 11800, -600)
wake_enable = create_scalar(master, "ShipWakeEnable", 1.0, 11800, -400)

height_custom = unreal.MaterialEditingLibrary.create_material_expression(
    master, unreal.MaterialExpressionCustom, 12300, -1200)
height_custom.set_editor_property("description", "SW Kelvin Wake WPO (SWShipWake.ush)")
set_desc(height_custom, "KELVIN M1 HEIGHT - connect only through generated pins")
height_custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
height_custom.set_editor_property("include_file_paths", ["/Project/SWShipWake.ush"])
height_custom.set_editor_property("inputs", [
    new_custom_input("WorldPosition"),
    new_custom_input("ShipWakeTex"),
    new_custom_input("ShipWakeServerTime"),
    new_custom_input("ShipWakeCount"),
    new_custom_input("HeightScale"),
    new_custom_input("Enable"),
])
height_custom.set_editor_property(
    "code",
    'float Height;\n'
    'float Foam;\n'
    'SW_EVALUATE_SHIP_WAKE('
    'WorldPosition.xy, ShipWakeServerTime, ShipWakeCount, ShipWakeTex, '
    'ShipWakeTexSampler, Height, Foam);\n'
    'return float3(0.0, 0.0, Height * HeightScale * saturate(Enable));')

for source, pin in (
    (world_position, "WorldPosition"),
    (wake_texture, "ShipWakeTex"),
    (wake_time, "ShipWakeServerTime"),
    (wake_count, "ShipWakeCount"),
    (wake_height_scale, "HeightScale"),
    (wake_enable, "Enable"),
):
    connect(source, "", height_custom, pin)

wpo_add = unreal.MaterialEditingLibrary.create_material_expression(
    master, unreal.MaterialExpressionAdd, 12850, -1150)
set_desc(wpo_add, "KELVIN M1: existing WPO + ship wake WPO")
existing_wpo = inputs_by_name["World Position Offset"]
existing_wpo_output = unreal.MaterialEditingLibrary.get_input_node_output_name_for_material_expression(
    final_attributes, existing_wpo) or ""
connect(existing_wpo, existing_wpo_output, wpo_add, "A")
connect(height_custom, "", wpo_add, "B")
connect(wpo_add, "", final_attributes, "World Position Offset")

foam_custom = unreal.MaterialEditingLibrary.create_material_expression(
    master, unreal.MaterialExpressionCustom, 12300, -300)
foam_custom.set_editor_property("description", "SW Kelvin Wake Foam Mask (SWShipWake.ush)")
set_desc(foam_custom, "KELVIN M1 FOAM - shared wake packets")
foam_custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT1)
foam_custom.set_editor_property("include_file_paths", ["/Project/SWShipWake.ush"])
foam_custom.set_editor_property("inputs", [
    new_custom_input("WorldPosition"),
    new_custom_input("ShipWakeTex"),
    new_custom_input("ShipWakeServerTime"),
    new_custom_input("ShipWakeCount"),
    new_custom_input("Enable"),
])
foam_custom.set_editor_property(
    "code",
    'float Height;\n'
    'float Foam;\n'
    'SW_EVALUATE_SHIP_WAKE('
    'WorldPosition.xy, ShipWakeServerTime, ShipWakeCount, ShipWakeTex, '
    'ShipWakeTexSampler, Height, Foam);\n'
    'return Foam * saturate(Enable);')
for source, pin in (
    (world_position, "WorldPosition"),
    (wake_texture, "ShipWakeTex"),
    (wake_time, "ShipWakeServerTime"),
    (wake_count, "ShipWakeCount"),
    (wake_enable, "Enable"),
):
    connect(source, "", foam_custom, pin)

foam_intensity = create_scalar(master, "ShipWakeFoamIntensity", 2.5, 12600, -100)
foam_color = create_vector(
    master, "ShipWakeFoamColor", unreal.LinearColor(0.72, 0.86, 0.92, 1.0), 12600, 100)
foam_scaled = unreal.MaterialEditingLibrary.create_material_expression(
    master, unreal.MaterialExpressionMultiply, 13000, -200)
foam_tinted = unreal.MaterialEditingLibrary.create_material_expression(
    master, unreal.MaterialExpressionMultiply, 13300, -100)
connect(foam_custom, "", foam_scaled, "A")
connect(foam_intensity, "", foam_scaled, "B")
connect(foam_scaled, "", foam_tinted, "A")
connect(foam_color, "", foam_tinted, "B")

emissive_add = unreal.MaterialEditingLibrary.create_material_expression(
    master, unreal.MaterialExpressionAdd, 13600, -50)
set_desc(emissive_add, "KELVIN M1: preserve existing emissive + wake foam")
existing_emissive = inputs_by_name["Emissive Color"]
existing_emissive_output = unreal.MaterialEditingLibrary.get_input_node_output_name_for_material_expression(
    final_attributes, existing_emissive) or ""
connect(existing_emissive, existing_emissive_output, emissive_add, "A")
connect(foam_tinted, "", emissive_add, "B")
connect(emissive_add, "", final_attributes, "Emissive Color")

roughness_one = unreal.MaterialEditingLibrary.create_material_expression(
    master, unreal.MaterialExpressionConstant, 13000, 350)
roughness_one.set_editor_property("r", 1.0)
roughness_lerp = unreal.MaterialEditingLibrary.create_material_expression(
    master, unreal.MaterialExpressionLinearInterpolate, 13600, 300)
set_desc(roughness_lerp, "KELVIN M1: foam drives roughness toward 1")
existing_roughness = inputs_by_name["Roughness"]
existing_roughness_output = unreal.MaterialEditingLibrary.get_input_node_output_name_for_material_expression(
    final_attributes, existing_roughness) or ""
connect(existing_roughness, existing_roughness_output, roughness_lerp, "A")
connect(roughness_one, "", roughness_lerp, "B")
connect(foam_custom, "", roughness_lerp, "Alpha")
connect(roughness_lerp, "", final_attributes, "Roughness")

unreal.MaterialEditingLibrary.recompile_material(master)
unreal.MaterialEditingLibrary.update_material_instance(instance)
for asset in (master, instance, waves, ship):
    unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False)

# Validation: copied assets exist, the instance points to the copied master,
# and the copied Blueprint inherits the native component owner class.
if instance.get_editor_property("parent") != master:
    raise RuntimeError("Kelvin material instance does not use the copied master")
if not isinstance(unreal.get_default_object(ship.generated_class()), unreal.KelvinShip):
    raise RuntimeError("Kelvin player Blueprint was not reparented to AKelvinShip")

unreal.log_warning(
    "[KelvinM1Assets] Created safe Kelvin copies and injected Custom-node WPO/foam graph: "
    f"{MASTER}, {INSTANCE}, {WAVES}, {SHIP}")
