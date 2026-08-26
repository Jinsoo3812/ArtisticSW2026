import unreal

errors = []
base = unreal.EditorAssetLibrary.load_asset("/Game/Blueprints/Ship/Enemy_Ship/Materials/M_ES_TorpedoPulse")
instance = unreal.EditorAssetLibrary.load_asset("/Game/Blueprints/Ship/Enemy_Ship/Materials/MI_ES_TorpedoPulse")
bp = unreal.EditorAssetLibrary.load_asset("/Game/Blueprints/Ship/Enemy_Ship/Blueprints/BP_ES_Torpedo")
if not base:
    errors.append("base material missing")
if not instance:
    errors.append("material instance missing")
elif instance.get_editor_property("parent") != base:
    errors.append("material instance parent mismatch")
if not bp:
    errors.append("torpedo BP missing")
else:
    cdo = unreal.get_default_object(bp.generated_class())
    if cdo.get_editor_property("pulse_overlay_material") != instance:
        errors.append("BP pulse overlay assignment mismatch")
if base:
    if base.get_editor_property("blend_mode") != unreal.BlendMode.BLEND_TRANSLUCENT:
        errors.append("blend mode is not translucent")

if errors:
    for error in errors:
        unreal.log_error("TORPEDO_PULSE_VALIDATE " + error)
    raise RuntimeError("Torpedo pulse validation failed")
unreal.log_warning("TORPEDO_PULSE_VALIDATE_OK")
