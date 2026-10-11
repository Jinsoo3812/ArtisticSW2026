"""Author a tiny procedural star material and the existing Stun cue's boss-only presentation."""
import unreal

FOLDER = "/Game/GameplayCues/Boss/Stun"
MATERIAL_PATH = FOLDER + "/M_BossStunStars"
CUE_PATH = FOLDER + "/GC_Boss_Stun"
tools = unreal.AssetToolsHelpers.get_asset_tools()
library = unreal.EditorAssetLibrary

if library.does_asset_exist(MATERIAL_PATH):
    material = library.load_asset(MATERIAL_PATH)
else:
    material = tools.create_asset("M_BossStunStars", FOLDER, unreal.Material, unreal.MaterialFactoryNew())
if not material:
    raise RuntimeError("Could not create star material")

editing = unreal.MaterialEditingLibrary
editing.delete_all_material_expressions(material)
material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
material.set_editor_property("two_sided", True)
material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)

uv = editing.create_material_expression(material, unreal.MaterialExpressionTextureCoordinate, -600, 180)
shape = editing.create_material_expression(material, unreal.MaterialExpressionCustom, -350, 180)
shape.set_editor_property("description", "Procedural five-point star: no texture or particle simulation")
shape.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT1)
uv_input = unreal.CustomInput()
uv_input.set_editor_property("input_name", "UV")
shape.set_editor_property("inputs", [uv_input])
shape.set_editor_property("code", """
float2 p = (UV - 0.5) * 2.1;
float angle = atan2(p.y, p.x) - 1.57079632679;
angle = angle - floor(angle / 6.28318530718) * 6.28318530718;
float sector = floor(angle / 0.628318530718);
float a0 = sector * 0.628318530718 + 1.57079632679;
float a1 = a0 + 0.628318530718;
float even = 1.0 - fmod(sector, 2.0);
float r0 = lerp(0.44, 1.0, even);
float r1 = lerp(1.0, 0.44, even);
float2 v0 = float2(cos(a0), sin(a0)) * r0;
float2 v1 = float2(cos(a1), sin(a1)) * r1;
float2 d = float2(cos(angle + 1.57079632679), sin(angle + 1.57079632679));
float2 edge = v1 - v0;
float boundary = (v0.x * edge.y - v0.y * edge.x) / (d.x * edge.y - d.y * edge.x);
return saturate((boundary - length(p)) * 80.0);
""")
editing.connect_material_expressions(uv, "", shape, "UV")
editing.connect_material_property(shape, "", unreal.MaterialProperty.MP_OPACITY_MASK)

color = editing.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -600, -180)
color.set_editor_property("parameter_name", "StarColor")
color.set_editor_property("default_value", unreal.LinearColor(1.0, 0.65, 0.08, 1.0))
glow = editing.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -600, -30)
glow.set_editor_property("parameter_name", "GlowIntensity")
glow.set_editor_property("default_value", 5.0)
emissive = editing.create_material_expression(material, unreal.MaterialExpressionMultiply, -330, -150)
editing.connect_material_expressions(color, "", emissive, "A")
editing.connect_material_expressions(glow, "", emissive, "B")
editing.connect_material_property(emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
editing.recompile_material(material)
if not library.save_loaded_asset(material, only_if_is_dirty=False):
    raise RuntimeError("Could not save star material")

parent = unreal.load_class(None, "/Script/Enemy.BossStunGameplayCue")
if not parent:
    raise RuntimeError("Build Enemy module before authoring this cue")
if library.does_asset_exist(CUE_PATH):
    cue = library.load_asset(CUE_PATH)
else:
    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", parent)
    cue = tools.create_asset("GC_Boss_Stun", FOLDER, unreal.Blueprint, factory)
if not cue:
    raise RuntimeError("Could not create boss Stun cue")
cdo = unreal.get_default_object(cue.generated_class())
tag = unreal.GameplayTag()
if not tag.import_text("GameplayCue.Status.Stun"):
    raise RuntimeError("Stun cue tag is unavailable")
cdo.set_editor_property("gameplay_cue_tag", tag)
cdo.set_editor_property("star_material", material)
if not library.save_loaded_asset(cue, only_if_is_dirty=False):
    raise RuntimeError("Could not save boss Stun cue")
unreal.log("Boss stun VFX saved: " + CUE_PATH + " (three golden stars; GE-owned lifetime)")
