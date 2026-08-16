import unreal


MATERIAL_PATHS = (
    "/Game/New/Water/Realistic_Water/M_Realistic_Water",
    "/Game/Tests/Landscape/Kelvin/M_Kelvin_RealisticWater",
)

WPO_CODE = """float Height;
SW_EVALUATE_SHIP_WAKE_M2(WorldPosition.xy, ShipWakeServerTime, ShipWakeCount, ShipWakeTex, ShipWakeTexSampler, Height);
return float3(0.0, 0.0, Height * HeightScale * saturate(Enable));"""

FOAM_CODE = """float Height;
float Foam;
SW_EVALUATE_SHIP_WAKE(WorldPosition.xy, ShipWakeServerTime, ShipWakeCount, ShipWakeTex, ShipWakeTexSampler, Height, Foam);
return Foam * saturate(Enable);"""


for material_path in MATERIAL_PATHS:
    material = unreal.load_asset(material_path)
    if not material:
        raise RuntimeError(f"Material not found: {material_path}")

    updated = []
    asset_name = material_path.rsplit("/", 1)[-1]
    prefix = f"{material_path}.{asset_name}:"
    for index in range(120):
        expression = unreal.load_object(None, f"{prefix}MaterialExpressionCustom_{index}")
        if not expression:
            continue
        description = str(expression.get_editor_property("description"))
        if "SW Kelvin Wake WPO" in description:
            expression.set_editor_property("code", WPO_CODE)
            expression.set_editor_property("include_file_paths", ["/Project/SWShipWake.ush"])
            updated.append(expression.get_name())
        elif "SW Kelvin Wake Foam Mask" in description:
            expression.set_editor_property("code", FOAM_CODE)
            expression.set_editor_property("include_file_paths", ["/Project/SWShipWake.ush"])
            updated.append(expression.get_name())

    if not updated:
        raise RuntimeError(f"Kelvin Custom expressions were not found: {material_path}")

    unreal.MaterialEditingLibrary.recompile_material(material)
    if not unreal.EditorAssetLibrary.save_loaded_asset(material, only_if_is_dirty=False):
        raise RuntimeError(f"Failed to save: {material_path}")

    unreal.log_warning(f"[KelvinCustomFix] PASS material={material_path} updated={updated}")
