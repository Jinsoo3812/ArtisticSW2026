import unreal


ROOT = "/Game/Tests/Landscape/Kelvin"
MASTER_PATH = f"{ROOT}/M_Kelvin_RealisticWater"
INSTANCE_PATH = f"{ROOT}/MI_Kelvin_RealisticWater_Ocean"
WAVES_PATH = f"{ROOT}/Kelvin_Waves_RealisticWater"
SHIP_PATH = f"{ROOT}/BP_PlayerShip_Kelvin"


def load(path):
    asset = unreal.load_asset(path)
    if not asset:
        raise RuntimeError(f"Missing Kelvin M1 asset: {path}")
    return asset


master = load(MASTER_PATH)
instance = load(INSTANCE_PATH)
waves = load(WAVES_PATH)
ship = load(SHIP_PATH)

if instance.get_editor_property("parent") != master:
    raise RuntimeError("Kelvin MI parent is not the copied Kelvin master")
unreal.BlueprintEditorLibrary.compile_blueprint(ship)
generated = ship.generated_class()
cdo = unreal.get_default_object(generated)
if not isinstance(cdo, unreal.KelvinShip):
    raise RuntimeError(f"Kelvin Blueprint CDO is not an AKelvinShip: {cdo.get_class().get_name()}")
emitter = cdo.get_editor_property("ship_wake_emitter")
if not emitter:
    raise RuntimeError("Kelvin ship CDO has no inherited ShipWakeEmitter")

final_attributes = unreal.load_object(
    None,
    f"{MASTER_PATH}.M_Kelvin_RealisticWater:MaterialExpressionSetMaterialAttributes_2")
if not final_attributes:
    raise RuntimeError("Kelvin material final attributes node is missing")

names = list(unreal.MaterialEditingLibrary.get_material_expression_input_names(final_attributes))
nodes = list(unreal.MaterialEditingLibrary.get_inputs_for_material_expression(master, final_attributes))
inputs = {str(name): nodes[index] for index, name in enumerate(names)}
expected_classes = {
    "World Position Offset": "MaterialExpressionAdd",
    "Roughness": "MaterialExpressionLinearInterpolate",
    "Emissive Color": "MaterialExpressionAdd",
}
for input_name, expected_class in expected_classes.items():
    node = inputs.get(input_name)
    if not node or node.get_class().get_name() != expected_class:
        raise RuntimeError(
            f"{input_name} is not connected through the Kelvin node; "
            f"got {node.get_class().get_name() if node else None}")

if unreal.MaterialEditingLibrary.get_num_material_expressions(master) < 50:
    raise RuntimeError("Kelvin copied master has an unexpectedly small material graph")

unreal.MaterialEditingLibrary.recompile_material(master)
unreal.MaterialEditingLibrary.update_material_instance(instance)
unreal.EditorAssetLibrary.save_loaded_asset(master, only_if_is_dirty=False)
unreal.EditorAssetLibrary.save_loaded_asset(instance, only_if_is_dirty=False)
unreal.EditorAssetLibrary.save_loaded_asset(ship, only_if_is_dirty=False)

unreal.log_warning(
    "[KelvinM1Validation] PASS "
    f"GeneratedClass={generated.get_name()} "
    f"Emitter={emitter.get_class().get_name()} "
    f"Expressions={unreal.MaterialEditingLibrary.get_num_material_expressions(master)} "
    f"Waves={waves.get_class().get_name()}")
