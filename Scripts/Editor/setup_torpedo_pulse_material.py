import unreal

FOLDER = "/Game/Blueprints/Ship/Enemy_Ship/Materials"
BASE_PATH = FOLDER + "/M_ES_TorpedoPulse"
INSTANCE_PATH = FOLDER + "/MI_ES_TorpedoPulse"
TORPEDO_BP_PATH = "/Game/Blueprints/Ship/Enemy_Ship/Blueprints/BP_ES_Torpedo"

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
unreal.EditorAssetLibrary.make_directory(FOLDER)

material = unreal.EditorAssetLibrary.load_asset(BASE_PATH)
if not material:
    material = asset_tools.create_asset("M_ES_TorpedoPulse", FOLDER, unreal.Material, unreal.MaterialFactoryNew())
if not material:
    raise RuntimeError("Could not create M_ES_TorpedoPulse")

material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
material.set_editor_property("two_sided", True)
unreal.MaterialEditingLibrary.delete_all_material_expressions(material)

def expr(cls, x, y):
    return unreal.MaterialEditingLibrary.create_material_expression(material, cls, x, y)

time = expr(unreal.MaterialExpressionTime, -900, -150)
speed = expr(unreal.MaterialExpressionScalarParameter, -900, 0)
speed.set_editor_property("parameter_name", "BlinkSpeed")
speed.set_editor_property("default_value", 2.0)
time_speed = expr(unreal.MaterialExpressionMultiply, -680, -100)
sine = expr(unreal.MaterialExpressionSine, -480, -100)
half = expr(unreal.MaterialExpressionConstant, -480, 60)
half.set_editor_property("r", 0.5)
scaled_sine = expr(unreal.MaterialExpressionMultiply, -280, -80)
normalized = expr(unreal.MaterialExpressionAdd, -80, -80)

intensity = expr(unreal.MaterialExpressionScalarParameter, -280, 100)
intensity.set_editor_property("parameter_name", "PulseIntensity")
intensity.set_editor_property("default_value", 3.0)
min_emissive = expr(unreal.MaterialExpressionScalarParameter, -280, 220)
min_emissive.set_editor_property("parameter_name", "MinimumEmissive")
min_emissive.set_editor_property("default_value", 0.05)
pulse_strength = expr(unreal.MaterialExpressionMultiply, 120, -60)
emissive_strength = expr(unreal.MaterialExpressionAdd, 320, -20)

color = expr(unreal.MaterialExpressionVectorParameter, 100, -260)
color.set_editor_property("parameter_name", "PulseColor")
color.set_editor_property("default_value", unreal.LinearColor(1.0, 0.035, 0.005, 1.0))
emissive = expr(unreal.MaterialExpressionMultiply, 540, -170)

base_opacity = expr(unreal.MaterialExpressionScalarParameter, 100, 180)
base_opacity.set_editor_property("parameter_name", "BaseOpacity")
base_opacity.set_editor_property("default_value", 0.04)
pulse_opacity = expr(unreal.MaterialExpressionScalarParameter, 100, 300)
pulse_opacity.set_editor_property("parameter_name", "PulseOpacity")
pulse_opacity.set_editor_property("default_value", 0.18)
opacity_pulse = expr(unreal.MaterialExpressionMultiply, 320, 220)
opacity = expr(unreal.MaterialExpressionAdd, 540, 220)

connect = unreal.MaterialEditingLibrary.connect_material_expressions
connect(time, "", time_speed, "A")
connect(speed, "", time_speed, "B")
connect(time_speed, "", sine, "Input")
connect(sine, "", scaled_sine, "A")
connect(half, "", scaled_sine, "B")
connect(scaled_sine, "", normalized, "A")
connect(half, "", normalized, "B")
connect(normalized, "", pulse_strength, "A")
connect(intensity, "", pulse_strength, "B")
connect(pulse_strength, "", emissive_strength, "A")
connect(min_emissive, "", emissive_strength, "B")
connect(color, "", emissive, "A")
connect(emissive_strength, "", emissive, "B")
connect(normalized, "", opacity_pulse, "A")
connect(pulse_opacity, "", opacity_pulse, "B")
connect(base_opacity, "", opacity, "A")
connect(opacity_pulse, "", opacity, "B")
unreal.MaterialEditingLibrary.connect_material_property(emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
unreal.MaterialEditingLibrary.connect_material_property(opacity, "", unreal.MaterialProperty.MP_OPACITY)
unreal.MaterialEditingLibrary.recompile_material(material)
unreal.EditorAssetLibrary.save_loaded_asset(material)

instance = unreal.EditorAssetLibrary.load_asset(INSTANCE_PATH)
if not instance:
    factory = unreal.MaterialInstanceConstantFactoryNew()
    instance = asset_tools.create_asset("MI_ES_TorpedoPulse", FOLDER, unreal.MaterialInstanceConstant, factory)
if not instance:
    raise RuntimeError("Could not create MI_ES_TorpedoPulse")
instance.set_editor_property("parent", material)
unreal.EditorAssetLibrary.save_loaded_asset(instance)

torpedo_bp = unreal.EditorAssetLibrary.load_asset(TORPEDO_BP_PATH)
if not torpedo_bp:
    raise RuntimeError("BP_ES_Torpedo missing")
cdo = unreal.get_default_object(torpedo_bp.generated_class())
cdo.set_editor_property("pulse_overlay_material", instance)
unreal.EditorAssetLibrary.save_loaded_asset(torpedo_bp)
unreal.log_warning("TORPEDO_PULSE_SETUP_COMPLETE")
