import unreal


MATERIAL_PATH = "/Game/Blueprints/Water/M_Realistic_Water"


material = unreal.load_asset(MATERIAL_PATH)
if not material:
    raise RuntimeError(f"Could not load {MATERIAL_PATH}")

expressions = unreal.RealisticWaterMaterialPipelineLibrary.get_material_expressions(material)
by_name = {expression.get_name(): expression for expression in expressions}

for name in (
    "MaterialExpressionWorldPosition_3",
    "MaterialExpressionComponentMask_9",
    "MaterialExpressionPixelNormalWS_0",
    "MaterialExpressionComponentMask_10",
    "MaterialExpressionCustom_16",
):
    expression = by_name.get(name)
    if not expression:
        unreal.log_warning(f"FOAM_INSPECT missing={name}")
        continue

    values = {"name": name, "class": expression.get_class().get_name()}
    for prop in ("r", "g", "b", "a", "world_position_shader_offset", "code", "desc"):
        try:
            values[prop] = expression.get_editor_property(prop)
        except Exception:
            pass
    unreal.log(f"FOAM_INSPECT {values}")

for expression in expressions:
    try:
        parameter_name = str(expression.get_editor_property("parameter_name"))
    except Exception:
        continue
    if any(token in parameter_name for token in ("Godot Normal", "Godot Mip", "Godot Normal Fade", "SlopeBounds", "CrestBounds")):
        values = {"name": expression.get_name(), "parameter": parameter_name}
        for prop in ("default_value",):
            try:
                values[prop] = expression.get_editor_property(prop)
            except Exception:
                pass
        unreal.log(f"FOAM_PARAM {values}")

