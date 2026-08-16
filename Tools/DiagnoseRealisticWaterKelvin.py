import unreal


MATERIAL_PATH = "/Game/New/Water/Realistic_Water/M_Realistic_Water"
INSTANCE_PATH = "/Game/New/Water/Realistic_Water/M_Realistic_Water_Ocean"
WAVES_PATH = "/Game/New/Water/Realistic_Water/Waves_Realistic_Water"
LEVEL_PATH = "/Game/New/Water/Realistic_Water/Realistic_Water"


def log(message):
    unreal.log_warning(f"[KelvinDiagnosis] {message}")


material = unreal.load_asset(MATERIAL_PATH)
instance = unreal.load_asset(INSTANCE_PATH)
waves = unreal.load_asset(WAVES_PATH)
if not material or not instance or not waves:
    raise RuntimeError("Required Realistic_Water asset is missing")

log(f"material={material.get_path_name()} expressions={unreal.MaterialEditingLibrary.get_num_material_expressions(material)}")
log(f"instance_parent={instance.get_editor_property('parent').get_path_name()}")
log(f"waves={waves.get_path_name()} class={waves.get_class().get_name()}")

final_node = unreal.MaterialEditingLibrary.get_material_property_input_node(
    material, unreal.MaterialProperty.MP_MATERIAL_ATTRIBUTES)
log(f"final_node={final_node.get_path_name() if final_node else None} class={final_node.get_class().get_name() if final_node else None}")
if final_node:
    names = list(unreal.MaterialEditingLibrary.get_material_expression_input_names(final_node))
    nodes = list(unreal.MaterialEditingLibrary.get_inputs_for_material_expression(material, final_node))
    for index, name in enumerate(names):
        node = nodes[index] if index < len(nodes) else None
        output = unreal.MaterialEditingLibrary.get_input_node_output_name_for_material_expression(final_node, node) if node else ""
        log(f"final_input[{index}]={name} node={node.get_name() if node else None} class={node.get_class().get_name() if node else None} output={output}")
        if str(name) == "World Position Offset" and node:
            upstream_names = list(unreal.MaterialEditingLibrary.get_material_expression_input_names(node))
            upstream_nodes = list(unreal.MaterialEditingLibrary.get_inputs_for_material_expression(material, node))
            for upstream_index, upstream_name in enumerate(upstream_names):
                upstream = upstream_nodes[upstream_index] if upstream_index < len(upstream_nodes) else None
                log(
                    f"wpo_input[{upstream_index}]={upstream_name} "
                    f"node={upstream.get_name() if upstream else None} "
                    f"class={upstream.get_class().get_name() if upstream else None}")

try:
    objects = unreal.get_objects_with_outer(material, include_nested_objects=True)
except Exception as exc:
    log(f"get_objects_with_outer failed={exc}")
    objects = []

# Python does not expose object iteration in every editor build. Probe the
# material expression subobjects by their stable generated names as a fallback.
if not objects:
    material_object_prefix = f"{MATERIAL_PATH}.M_Realistic_Water:"
    for class_name in (
        "MaterialExpressionCustom",
        "MaterialExpressionAdd",
        "MaterialExpressionBreakMaterialAttributes",
        "MaterialExpressionSetMaterialAttributes",
        "MaterialExpressionScalarParameter",
        "MaterialExpressionTextureObjectParameter",
    ):
        for index in range(0, 80):
            obj = unreal.load_object(None, f"{material_object_prefix}{class_name}_{index}")
            if obj:
                objects.append(obj)

for obj in objects:
    if not isinstance(obj, unreal.MaterialExpressionCustom):
        continue
    try:
        description = obj.get_editor_property("description")
    except Exception:
        description = ""
    try:
        code = obj.get_editor_property("code")
    except Exception:
        code = ""
    try:
        includes = obj.get_editor_property("include_file_paths")
    except Exception as exc:
        includes = f"<error {exc}>"
    input_names = [str(value.get_editor_property("input_name")) for value in obj.get_editor_property("inputs")]
    if "Kelvin" in str(description) or "ShipWake" in str(code) or any("ShipWake" in name for name in input_names):
        log(
            f"custom={obj.get_name()} description={description} output_type={obj.get_editor_property('output_type')} "
            f"includes={includes} inputs={input_names} code={code}")

        custom_nodes = list(unreal.MaterialEditingLibrary.get_inputs_for_material_expression(material, obj))
        for custom_index, input_name in enumerate(input_names):
            expression = custom_nodes[custom_index] if custom_index < len(custom_nodes) else None
            log(
                f"CUSTOM_INPUT index={custom_index} name={input_name} "
                f"node={expression.get_name() if expression else None} "
                f"class={expression.get_class().get_name() if expression else None}")

for obj in objects:
    if isinstance(obj, unreal.MaterialExpressionAdd):
        upstream_names = list(unreal.MaterialEditingLibrary.get_material_expression_input_names(obj))
        upstream_nodes = list(unreal.MaterialEditingLibrary.get_inputs_for_material_expression(material, obj))
        upstream_summary = []
        for index, name in enumerate(upstream_names):
            upstream = upstream_nodes[index] if index < len(upstream_nodes) else None
            upstream_output = (
                unreal.MaterialEditingLibrary.get_input_node_output_name_for_material_expression(obj, upstream)
                if upstream else "")
            upstream_summary.append(
                f"{name}={upstream.get_name() if upstream else None}/"
                f"{upstream.get_class().get_name() if upstream else None}/"
                f"output={upstream_output}")
        if any("Custom" in value or "BreakMaterialAttributes" in value for value in upstream_summary):
            log(f"candidate_wpo_add={obj.get_name()} inputs={upstream_summary}")

for obj in objects:
    if isinstance(obj, unreal.MaterialExpressionSetMaterialAttributes):
        input_names = list(unreal.MaterialEditingLibrary.get_material_expression_input_names(obj))
        input_nodes = list(unreal.MaterialEditingLibrary.get_inputs_for_material_expression(material, obj))
        for index, input_name in enumerate(input_names):
            input_node = input_nodes[index] if index < len(input_nodes) else None
            log(
                f"SET_ATTRIBUTE node={obj.get_name()} index={index} name={input_name} "
                f"input_node={input_node.get_name() if input_node else None} "
                f"input_class={input_node.get_class().get_name() if input_node else None}")

for obj in objects:
    if isinstance(obj, unreal.MaterialExpressionScalarParameter):
        parameter_name = str(obj.get_editor_property("parameter_name"))
        if "ShipWake" in parameter_name or "Kelvin" in parameter_name:
            log(
                f"SCALAR_PARAMETER node={obj.get_name()} name={parameter_name} "
                f"default={obj.get_editor_property('default_value')}")

try:
    scalar_values = unreal.MaterialEditingLibrary.get_scalar_parameter_names(instance)
except Exception as exc:
    scalar_values = []
    log(f"instance_scalar_names_error={exc}")
for parameter_name in scalar_values:
    if "ShipWake" in str(parameter_name) or "Kelvin" in str(parameter_name):
        try:
            value = unreal.MaterialEditingLibrary.get_material_instance_scalar_parameter_value(instance, parameter_name)
        except Exception as exc:
            value = f"<error {exc}>"
        log(f"INSTANCE_SCALAR name={parameter_name} value={value}")

world = unreal.EditorLoadingAndSavingUtils.load_map(LEVEL_PATH)
if not world:
    raise RuntimeError(f"Failed to load level: {LEVEL_PATH}")

actors = unreal.EditorLevelLibrary.get_all_level_actors()
for actor in actors:
    actor_class = actor.get_class().get_name()
    if "WaterBody" in actor_class:
        component = actor.get_component_by_class(unreal.WaterBodyComponent)
        material_value = None
        waves_value = None
        if component:
            for prop_name in ("water_material", "water_waves"):
                try:
                    value = component.get_editor_property(prop_name)
                except Exception as exc:
                    value = f"<error {exc}>"
                if prop_name == "water_material":
                    material_value = value
                else:
                    waves_value = value
        log(
            f"water_actor={actor.get_name()} class={actor_class} "
            f"material={material_value.get_path_name() if hasattr(material_value, 'get_path_name') else material_value} "
            f"waves={waves_value.get_path_name() if hasattr(waves_value, 'get_path_name') else waves_value}")
        log(
            "water_actor_wave_methods="
            + ",".join(name for name in dir(actor) if "wave" in name.lower()))
        log(
            "water_component_wave_methods="
            + ",".join(name for name in dir(component) if "wave" in name.lower()) if component else "")
        for label, getter in (
            ("actor.water_waves", lambda: actor.get_editor_property("water_waves")),
            ("component.get_water_waves", lambda: component.get_water_waves() if component else None),
        ):
            try:
                wave_object = getter()
                log(
                    f"{label}={wave_object.get_path_name() if wave_object else None} "
                    f"class={wave_object.get_class().get_name() if wave_object else None}")
                if wave_object:
                    try:
                        base_asset = wave_object.get_editor_property("base_waves_asset")
                        log(
                            f"{label}.base_waves_asset="
                            f"{base_asset.get_path_name() if base_asset else None}")
                    except Exception as exc:
                        log(f"{label}.base_waves_asset_error={exc}")
            except Exception as exc:
                log(f"{label}_error={exc}")

    if "BP_PlayerShip_Kelvin" in actor_class or "BP_PlayerShip_Kelvin" in actor.get_name():
        emitter = actor.get_component_by_class(unreal.SWShipWakeEmitterComponent)
        log(
            f"kelvin_ship={actor.get_name()} class={actor_class} emitter={emitter} "
            f"location={actor.get_actor_location()} velocity={actor.get_velocity()}")
        if emitter:
            for prop_name in (
                "minimum_speed_cm_per_second",
                "minimum_emission_interval",
                "emission_distance_cm",
                "maximum_amplitude_cm",
                "lifetime_seconds",
            ):
                log(f"emitter.{prop_name}={emitter.get_editor_property(prop_name)}")

log("inspection_complete")
