"""Add optimized single-ship cabin-volume clipping to the SLW master material."""
import traceback

import unreal


MASTER_PATH = "/Game/Blueprints/Water/M_Realistic_Water"
MPC_PATH = "/Game/Blueprints/Water/MPC_Water_Custom"
MASK_PATH = "/Game/Blueprints/Water/Culling/VT_SW_ShipCabinMask"
DATA_PATH = "/Game/Blueprints/Water/Culling/DA_SW_ShipCabinWaterCull"
GROUP = "SW Cabin Water Culling"
CUSTOM_DESC = "SW Cabin Water Cull (bounds-gated volume sample)"
WORLD_DESC = "SW Cabin Cull Displaced World Position"

CUSTOM_CODE = r"""if (Enabled < 0.5)
{
    return 1.0;
}

float3 LocalPosition;
LocalPosition.x = dot(float4(WorldPosition, 1.0), InvRow0);
LocalPosition.y = dot(float4(WorldPosition, 1.0), InvRow1);
LocalPosition.z = dot(float4(WorldPosition, 1.0), InvRow2);

float3 Extent = max(LocalMax.xyz - LocalMin.xyz, float3(0.001, 0.001, 0.001));
float3 UVW = (LocalPosition - LocalMin.xyz) / Extent;

// Important optimization: do not issue the 3D texture sample outside the one ship's bounds.
[branch]
if (any(UVW < 0.0) || any(UVW > 1.0))
{
    return (DebugView > 1.5) ? 0.0 : 1.0;
}

if (DebugView > 0.5 && DebugView < 1.5)
{
    return 0.0;
}

float Occupancy = Texture3DSampleLevel(CabinMask, CabinMaskSampler, UVW, 0.0).r;
if (DebugView > 1.5)
{
    return (Occupancy >= Threshold) ? 1.0 : 0.0;
}
return (Occupancy >= Threshold) ? 0.0 : 1.0;"""


def prop(obj, name, default=None):
    try:
        return obj.get_editor_property(name)
    except Exception:
        return default


def short_name(expression):
    return expression.get_path_name().split(":")[-1] if expression else "None"


def find_by_desc(expressions, wanted):
    return next((e for e in expressions
                 if str(prop(e, "description", prop(e, "desc", ""))) == wanted), None)


def connect(source, output_name, target, input_name):
    if not unreal.MaterialEditingLibrary.connect_material_expressions(
            source, output_name, target, input_name):
        raise RuntimeError("Connection failed: {}.{} -> {}.{}".format(
            short_name(source), output_name, short_name(target), input_name))


def collection_node(material, expressions, mpc, name, x, y):
    node = next((e for e in expressions
                 if isinstance(e, unreal.MaterialExpressionCollectionParameter)
                 and str(prop(e, "parameter_name", "")) == name), None)
    if node is None:
        node = unreal.MaterialEditingLibrary.create_material_expression(
            material, unreal.MaterialExpressionCollectionParameter, x, y)
        expressions.append(node)
    if not unreal.RealisticWaterMaterialPipelineLibrary.configure_collection_parameter_expression(
            node, mpc, name):
        raise RuntimeError("Could not configure MPC expression " + name)
    return node


def main():
    material = unreal.load_asset(MASTER_PATH)
    mpc = unreal.load_asset(MPC_PATH)
    mask = unreal.load_asset(MASK_PATH)
    data = unreal.load_asset(DATA_PATH)
    if material is None or mpc is None or mask is None or data is None:
        raise RuntimeError("Required water material/MPC/cabin mask asset is missing")

    helper = unreal.RealisticWaterMaterialPipelineLibrary
    editing = unreal.MaterialEditingLibrary
    if not helper.configure_cabin_water_cull_collection(mpc):
        raise RuntimeError("Could not configure water MPC")
    if not helper.set_cabin_water_cull_bounds_defaults(
            mpc,
            data.get_editor_property("local_bounds_min"),
            data.get_editor_property("local_bounds_max")):
        raise RuntimeError("Could not store baked cabin bounds in MPC defaults")
    unreal.EditorAssetLibrary.save_loaded_asset(mpc, only_if_is_dirty=False)

    expressions = list(helper.get_material_expressions(material))
    final_attributes = next((e for e in expressions
                             if short_name(e) == "MaterialExpressionSetMaterialAttributes_2"), None)
    if final_attributes is None:
        raise RuntimeError("Final SetMaterialAttributes node is missing")

    world_position = find_by_desc(expressions, WORLD_DESC)
    if world_position is None:
        world_position = editing.create_material_expression(
            material, unreal.MaterialExpressionWorldPosition, 9200, 6160)
        world_position.set_editor_property("desc", WORLD_DESC)
        expressions.append(world_position)

    texture = next((e for e in expressions
                    if isinstance(e, unreal.MaterialExpressionTextureObjectParameter)
                    and str(prop(e, "parameter_name", "")) == "SW Cabin Cull Mask"), None)
    if texture is None:
        texture = editing.create_material_expression(
            material, unreal.MaterialExpressionTextureObjectParameter, 9200, 6280)
        expressions.append(texture)
    texture.set_editor_property("parameter_name", "SW Cabin Cull Mask")
    texture.set_editor_property("texture", mask)
    texture.set_editor_property("group", GROUP)

    names = [
        "SW_CabinCullInvRow0", "SW_CabinCullInvRow1", "SW_CabinCullInvRow2",
        "SW_CabinCullLocalMin", "SW_CabinCullLocalMax",
        "SW_CabinCullEnabled", "SW_CabinCullThreshold", "SW_CabinCullDebugView"]
    nodes = {}
    for index, name in enumerate(names):
        nodes[name] = collection_node(
            material, expressions, mpc, name,
            9200 + (index % 2) * 260, 6420 + (index // 2) * 120)

    custom = find_by_desc(expressions, CUSTOM_DESC)
    if custom is None:
        custom = editing.create_material_expression(
            material, unreal.MaterialExpressionCustom, 9860, 6160)
        expressions.append(custom)
    input_names = [
        "WorldPosition", "CabinMask", "InvRow0", "InvRow1", "InvRow2",
        "LocalMin", "LocalMax", "Enabled", "Threshold", "DebugView"]
    if not helper.configure_float1_custom_expression(
            custom, input_names, CUSTOM_CODE, CUSTOM_DESC):
        raise RuntimeError("Could not configure cabin-cull Custom expression")

    connect(world_position, "", custom, "WorldPosition")
    connect(texture, "", custom, "CabinMask")
    mapping = {
        "InvRow0": "SW_CabinCullInvRow0",
        "InvRow1": "SW_CabinCullInvRow1",
        "InvRow2": "SW_CabinCullInvRow2",
        "LocalMin": "SW_CabinCullLocalMin",
        "LocalMax": "SW_CabinCullLocalMax",
        "Enabled": "SW_CabinCullEnabled",
        "Threshold": "SW_CabinCullThreshold",
        "DebugView": "SW_CabinCullDebugView",
    }
    for input_name, parameter_name in mapping.items():
        connect(nodes[parameter_name], "", custom, input_name)

    # This water master had no explicit final Opacity Mask override before this
    # integration; the upstream water path is uniformly present (1). The custom
    # node therefore becomes the sole final mask without changing Opacity,
    # BaseColor, WPO, scattering or absorption.
    if not helper.connect_opacity_mask_attribute(final_attributes, custom):
        raise RuntimeError("Could not connect final Opacity Mask")

    helper.initialize_missing_parameter_guids(material)
    editing.recompile_material(material)
    unreal.EditorAssetLibrary.save_loaded_asset(material, only_if_is_dirty=False)
    unreal.log("SW_CABIN_CULL_INTEGRATION=PASS")
    unreal.log("SW_CABIN_CULL_EARLY_BOUNDS_BRANCH=1")
    unreal.log("SW_CABIN_CULL_VOLUME_SAMPLES_INSIDE_BOUNDS_ONLY=1")


try:
    main()
except Exception:
    unreal.log_error(traceback.format_exc())
    raise
