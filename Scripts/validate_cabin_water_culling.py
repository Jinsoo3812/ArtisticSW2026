"""Validate assets, graph wiring and optimization invariants for cabin water clipping."""
import traceback

import unreal


MASTER_PATH = "/Game/Blueprints/Water/M_Realistic_Water"
MPC_PATH = "/Game/Blueprints/Water/MPC_Water_Custom"
MASK_PATH = "/Game/Blueprints/Water/Culling/VT_SW_ShipCabinMask"
DATA_PATH = "/Game/Blueprints/Water/Culling/DA_SW_ShipCabinWaterCull"
CUSTOM_TOKEN = "Texture3DSampleLevel(CabinMask"


def prop(obj, name, default=None):
    try:
        return obj.get_editor_property(name)
    except Exception:
        return default


def short_name(expression):
    return expression.get_path_name().split(":")[-1] if expression else "None"


def main():
    material = unreal.load_asset(MASTER_PATH)
    mpc = unreal.load_asset(MPC_PATH)
    mask = unreal.load_asset(MASK_PATH)
    data = unreal.load_asset(DATA_PATH)
    if any(asset is None for asset in (material, mpc, mask, data)):
        raise RuntimeError("One or more culling pipeline assets are missing")
    if material.get_editor_property("blend_mode") != unreal.BlendMode.BLEND_MASKED:
        raise RuntimeError("Water master must remain Masked")
    if material.get_editor_property("shading_model") != unreal.MaterialShadingModel.MSM_SINGLE_LAYER_WATER:
        raise RuntimeError("Water master must remain Single Layer Water")
    if data.get_editor_property("mask_texture") != mask:
        raise RuntimeError("Cull Data Asset does not reference the baked Volume Texture")
    resolution = data.get_editor_property("resolution")
    if (resolution.x, resolution.y, resolution.z) != (359, 141, 298):
        raise RuntimeError("Unexpected baked resolution: {}".format(resolution))

    scalar_names = {str(p.get_editor_property("parameter_name"))
                    for p in mpc.get_editor_property("scalar_parameters")}
    vector_names = {str(p.get_editor_property("parameter_name"))
                    for p in mpc.get_editor_property("vector_parameters")}
    vector_defaults = {str(p.get_editor_property("parameter_name")):
                       p.get_editor_property("default_value")
                       for p in mpc.get_editor_property("vector_parameters")}
    required_scalars = {"SW_CabinCullEnabled", "SW_CabinCullThreshold", "SW_CabinCullDebugView"}
    required_vectors = {
        "SW_CabinCullInvRow0", "SW_CabinCullInvRow1", "SW_CabinCullInvRow2",
        "SW_CabinCullLocalMin", "SW_CabinCullLocalMax"}
    if not required_scalars.issubset(scalar_names):
        raise RuntimeError("Missing cull MPC scalar parameters")
    if not required_vectors.issubset(vector_names):
        raise RuntimeError("Missing cull MPC vector parameters")
    local_min = data.get_editor_property("local_bounds_min")
    local_max = data.get_editor_property("local_bounds_max")
    min_default = vector_defaults["SW_CabinCullLocalMin"]
    max_default = vector_defaults["SW_CabinCullLocalMax"]
    if max(abs(min_default.r - local_min.x), abs(min_default.g - local_min.y),
           abs(min_default.b - local_min.z)) > 0.01:
        raise RuntimeError("MPC LocalMin default does not match baked data")
    if max(abs(max_default.r - local_max.x), abs(max_default.g - local_max.y),
           abs(max_default.b - local_max.z)) > 0.01:
        raise RuntimeError("MPC LocalMax default does not match baked data")

    helper = unreal.RealisticWaterMaterialPipelineLibrary
    expressions = list(helper.get_material_expressions(material))
    custom = next((e for e in expressions
                   if isinstance(e, unreal.MaterialExpressionCustom)
                   and CUSTOM_TOKEN in str(prop(e, "code", ""))), None)
    if custom is None:
        raise RuntimeError("Cabin-cull Custom expression is missing")
    code = str(prop(custom, "code", ""))
    branch_index = code.find("if (any(UVW")
    sample_index = code.find(CUSTOM_TOKEN)
    if branch_index < 0 or sample_index < 0 or branch_index >= sample_index:
        raise RuntimeError("Bounds early-out must precede the Volume Texture sample")
    if len([helper.get_connected_input_expression(custom, i) for i in range(10)]) != 10:
        raise RuntimeError("Unexpected custom input count")
    for index in range(10):
        if helper.get_connected_input_expression(custom, index) is None:
            raise RuntimeError("Custom input {} is disconnected".format(index))

    final_attributes = next((e for e in expressions
                             if short_name(e) == "MaterialExpressionSetMaterialAttributes_2"), None)
    if final_attributes is None:
        raise RuntimeError("Final material nodes are missing")
    connected = False
    for index in range(16):
        if helper.get_connected_input_expression(final_attributes, index) == custom:
            connected = True
            break
    if not connected:
        raise RuntimeError("Cull mask is not connected to final material attributes")

    unreal.MaterialEditingLibrary.recompile_material(material)
    unreal.log("SW_CABIN_CULL_VALIDATION=PASS")
    unreal.log("SW_CABIN_CULL_BLEND=MASKED")
    unreal.log("SW_CABIN_CULL_SHADING=SINGLE_LAYER_WATER")
    unreal.log("SW_CABIN_CULL_RESOLUTION={}x{}x{}".format(
        resolution.x, resolution.y, resolution.z))
    unreal.log("SW_CABIN_CULL_OPTIMIZATION=BOUNDS_BEFORE_VOLUME_SAMPLE")


try:
    main()
except Exception:
    unreal.log_error(traceback.format_exc())
    raise
