"""Integrate direct current-frame Kelvin/Ripple Foam into the water material."""
import traceback
import unreal

ASSET_PATH = "/Game/Blueprints/Water/M_Realistic_Water"
DIRECT_DESC = "SW Direct Kelvin Ripple Foam Sample (no history)"
WORLD_DESC = "SW Improved Foam World Position (excluding WPO)"

DIRECT_CODE = r"""float KRaw=0.0;
if (KelvinEnabled > 0.5)
{
    float2 KUV=(WorldPos.xy-KelvinGridCenter.xy)/max(KelvinGridSize,1.0)+0.5;
    float KInside=step(0.0,KUV.x)*step(KUV.x,1.0)*step(0.0,KUV.y)*step(KUV.y,1.0);
    KRaw=KelvinFoamSource.SampleLevel(Material.Clamp_WorldGroupSettings,saturate(KUV),0.0).r*KInside;
}
float RRaw=0.0;
if (RippleEnabled > 0.5)
{
    float2 RUV=(WorldPos.xy-RippleGridCenter.xy)/max(RippleGridSize,1.0)+0.5;
    float RInside=step(0.0,RUV.x)*step(RUV.x,1.0)*step(0.0,RUV.y)*step(RUV.y,1.0);
    RRaw=RippleFoamSource.SampleLevel(Material.Clamp_WorldGroupSettings,saturate(RUV),0.0).r*RInside;
}
float K=saturate(smoothstep(min(KelvinDisplayMin,KelvinDisplayMax-0.00001),max(KelvinDisplayMax,KelvinDisplayMin+0.00001),KRaw)*KelvinIntensity);
float R=saturate(smoothstep(min(RippleDisplayMin,RippleDisplayMax-0.00001),max(RippleDisplayMax,RippleDisplayMin+0.00001),RRaw)*RippleIntensity);
return float3(K,R,1.0-(1.0-K)*(1.0-R));"""

OCEAN_CODE = r"""float H=GerstnerWPO.z;
float S=1.0-saturate(normalize(GerstnerNormal).z);
float slopeMask=saturate(smoothstep(SlopeBounds.r,SlopeBounds.g,H)*pow(smoothstep(SlopeBounds.b,SlopeBounds.a,S),2.0));
float crestMask=saturate(smoothstep(CrestBounds.r,CrestBounds.g,H)*smoothstep(CrestBounds.b,CrestBounds.a,S));
float3 emerald=ScaleFoam*TranslucentColor*SSSIntensity;
float3 white=WhiteFoam*FoamColor;
float3 gerstner=lerp(emerald*slopeMask,white,crestMask);
float density=1.0-(1.0-saturate(DirectFoam.r))*(1.0-saturate(DirectFoam.g));
float3 direct=lerp(emerald*density,white,smoothstep(0.25,0.85,density));
return max(gerstner,direct);"""

def name(e): return e.get_path_name().split(":")[-1]

def parameter(expressions, wanted):
    for e in expressions:
        try:
            if str(e.get_editor_property("parameter_name")) == wanted: return e
        except Exception: pass
    return None

def described(expressions, wanted):
    for e in expressions:
        try:
            if str(e.get_editor_property("desc")) == wanted: return e
        except Exception: pass
    return None

def texture_parameter(material, editing, expressions, pname, x, y):
    e=parameter(expressions,pname)
    if not e:
        e=editing.create_material_expression(material,unreal.MaterialExpressionTextureObjectParameter,x,y)
        e.set_editor_property("parameter_name",pname)
        e.set_editor_property("texture",unreal.load_asset("/Engine/EngineResources/Black.Black"))
        e.set_editor_property("sampler_type",unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
    return e

def scalar_parameter(material, editing, expressions, pname, default, x, y):
    e=parameter(expressions,pname)
    if not e:
        e=editing.create_material_expression(material,unreal.MaterialExpressionScalarParameter,x,y)
        e.set_editor_property("parameter_name",pname)
    e.set_editor_property("default_value",default)
    return e

def main():
    material=unreal.load_asset(ASSET_PATH)
    helper=unreal.RealisticWaterMaterialPipelineLibrary
    editing=unreal.MaterialEditingLibrary
    expressions=list(helper.get_material_expressions(material))
    ocean=next((e for e in expressions if name(e)=="MaterialExpressionCustom_16"),None)
    if not isinstance(ocean,unreal.MaterialExpressionCustom): raise RuntimeError("Ocean Custom_16 missing")
    original=[helper.get_connected_input_expression(ocean,i) for i in range(9)]
    if not all(original): raise RuntimeError("Ocean legacy inputs disconnected")
    gerstner=None
    for e in expressions:
        if isinstance(e,unreal.MaterialExpressionMaterialFunctionCall):
            try: f=e.get_editor_property("material_function")
            except Exception: f=None
            if f and "ComputeGerstnerWaves" in f.get_path_name(): gerstner=e; break
    if not gerstner: raise RuntimeError("Gerstner function missing")

    world=described(expressions,WORLD_DESC)
    if not world:
        world=editing.create_material_expression(material,unreal.MaterialExpressionWorldPosition,4300,2900)
        world.set_editor_property("desc",WORLD_DESC)
    world.set_editor_property("world_position_shader_offset",unreal.WorldPositionIncludedOffsets.WPT_EXCLUDE_ALL_SHADER_OFFSETS)
    kc,ks=parameter(expressions,"ShipWakeGridCenter"),parameter(expressions,"ShipWakeGridSize")
    rc,rs=parameter(expressions,"RippleGridCenter"),parameter(expressions,"RippleGridSize")
    if not all((kc,ks,rc,rs)): raise RuntimeError("Foam grid parameters missing")
    kt=texture_parameter(material,editing,expressions,"SW Kelvin Foam Source",4300,3100)
    rt=texture_parameter(material,editing,expressions,"SW Ripple Foam Source",4300,3260)
    specs=[("SW Kelvin Foam Enabled",0.0),("SW Kelvin Foam Intensity",1.0),("SW Kelvin Foam Display Min",0.02),("SW Kelvin Foam Display Max",0.60),("SW Ripple Foam Enabled",0.0),("SW Ripple Foam Intensity",1.0),("SW Ripple Foam Display Min",0.02),("SW Ripple Foam Display Max",0.60)]
    sp={p:scalar_parameter(material,editing,expressions,p,d,4300,3420+i*80) for i,(p,d) in enumerate(specs)}

    direct=helper.get_connected_input_expression(ocean,11)
    try: valid=isinstance(direct,unreal.MaterialExpressionCustom) and "KelvinFoamSource.SampleLevel" in str(direct.get_editor_property("code"))
    except Exception: valid=False
    if not valid: direct=described(expressions,DIRECT_DESC)
    if not direct: direct=editing.create_material_expression(material,unreal.MaterialExpressionCustom,4720,3200)
    inputs=["KelvinFoamSource","RippleFoamSource","WorldPos","KelvinGridCenter","KelvinGridSize","RippleGridCenter","RippleGridSize","KelvinEnabled","KelvinIntensity","KelvinDisplayMin","KelvinDisplayMax","RippleEnabled","RippleIntensity","RippleDisplayMin","RippleDisplayMax"]
    if not helper.configure_float3_custom_expression(direct,inputs,DIRECT_CODE,DIRECT_DESC): raise RuntimeError("Direct custom configure failed")
    links=[(kt,"","KelvinFoamSource"),(rt,"","RippleFoamSource"),(world,"XYZ","WorldPos"),(kc,"RGB","KelvinGridCenter"),(ks,"","KelvinGridSize"),(rc,"RGB","RippleGridCenter"),(rs,"","RippleGridSize"),(sp["SW Kelvin Foam Enabled"],"","KelvinEnabled"),(sp["SW Kelvin Foam Intensity"],"","KelvinIntensity"),(sp["SW Kelvin Foam Display Min"],"","KelvinDisplayMin"),(sp["SW Kelvin Foam Display Max"],"","KelvinDisplayMax"),(sp["SW Ripple Foam Enabled"],"","RippleEnabled"),(sp["SW Ripple Foam Intensity"],"","RippleIntensity"),(sp["SW Ripple Foam Display Min"],"","RippleDisplayMin"),(sp["SW Ripple Foam Display Max"],"","RippleDisplayMax")]
    for src,out,dst in links:
        if not editing.connect_material_expressions(src,out,direct,dst): raise RuntimeError("Direct link failed: "+dst)

    ocean_inputs=["ScaleFoam","WhiteFoam","WaveHeight","WaveSteepness","FoamColor","TranslucentColor","SlopeBounds","CrestBounds","SSSIntensity","GerstnerWPO","GerstnerNormal","DirectFoam"]
    if not helper.configure_float3_custom_expression(ocean,ocean_inputs,OCEAN_CODE,"Gerstner legacy + direct Kelvin/Ripple Foam"): raise RuntimeError("Ocean configure failed")
    outputs=["","","","","","","RGBA","RGBA",""]
    for dst,src,out in zip(ocean_inputs[:9],original,outputs):
        if not editing.connect_material_expressions(src,out,ocean,dst): raise RuntimeError("Ocean legacy link failed: "+dst)
    if not editing.connect_material_expressions(gerstner,"WPO",ocean,"GerstnerWPO"): raise RuntimeError("Gerstner WPO link failed")
    if not editing.connect_material_expressions(gerstner,"Normal",ocean,"GerstnerNormal"): raise RuntimeError("Gerstner Normal link failed")
    if not editing.connect_material_expressions(direct,"",ocean,"DirectFoam"): raise RuntimeError("Direct Foam link failed")

    for e in list(helper.get_material_expressions(material)):
        if e in (direct,ocean): continue
        remove=False
        if isinstance(e,unreal.MaterialExpressionCustom):
            try: remove="ImprovedFoamState.SampleLevel" in str(e.get_editor_property("code"))
            except Exception: pass
        try: remove=remove or str(e.get_editor_property("parameter_name")) in ("SW Improved Foam State","SW Improved Foam Enable")
        except Exception: pass
        if remove: editing.delete_material_expression(material,e)
    helper.initialize_missing_parameter_guids(material)
    editing.recompile_material(material)
    unreal.EditorAssetLibrary.save_loaded_asset(material,only_if_is_dirty=False)

try: main()
except Exception:
    unreal.log_error(traceback.format_exc()); raise
