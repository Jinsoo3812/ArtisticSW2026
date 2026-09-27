import unreal


INSTANCE_PATH = "/Game/Blueprints/Water/M_Realistic_Water_Ocean"
MPC_PATH = "/Game/Blueprints/Water/MPC_Water_Custom"


def main():
    instance = unreal.load_asset(INSTANCE_PATH)
    unreal.log("SW_CULL_INSTANCE class={} path={}".format(
        instance.get_class().get_name(), instance.get_path_name()))

    current = instance
    depth = 0
    while current is not None and depth < 8:
        unreal.log("SW_CULL_PARENT depth={} class={} path={}".format(
            depth, current.get_class().get_name(), current.get_path_name()))
        if not hasattr(current, "get_editor_property"):
            break
        try:
            current = current.get_editor_property("parent")
        except Exception:
            break
        depth += 1

    base = current
    unreal.log("SW_CULL_BASE path={} blend={} shading={}".format(
        base.get_path_name(),
        base.get_editor_property("blend_mode"),
        base.get_editor_property("shading_model")))
    expressions = unreal.RealisticWaterMaterialPipelineLibrary.get_material_expressions(base)
    unreal.log("SW_CULL_EXPRESSION_COUNT={}".format(len(expressions)))
    for expression in expressions:
        unreal.log("SW_CULL_EXPR class={} desc={} name={}".format(
            expression.get_class().get_name(),
            str(expression.get_editor_property("desc")),
            expression.get_name()))

    mpc = unreal.load_asset(MPC_PATH)
    unreal.log("SW_CULL_MPC path={}".format(mpc.get_path_name()))
    for parameter in mpc.get_editor_property("scalar_parameters"):
        unreal.log("SW_CULL_MPC_SCALAR name={} default={}".format(
            parameter.get_editor_property("parameter_name"),
            parameter.get_editor_property("default_value")))
    for parameter in mpc.get_editor_property("vector_parameters"):
        unreal.log("SW_CULL_MPC_VECTOR name={} default={}".format(
            parameter.get_editor_property("parameter_name"),
            parameter.get_editor_property("default_value")))


main()
