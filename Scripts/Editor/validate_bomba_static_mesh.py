import unreal

root = "/Game/Blueprints/Ship/Enemy_Ship/Torpeodo"
mesh = unreal.EditorAssetLibrary.load_asset(root + "/SM_Bomba")
material = unreal.EditorAssetLibrary.load_asset(root + "/M_Bomba")
normal = unreal.EditorAssetLibrary.load_asset(root + "/textures/Bomba_Normal_OpenGL")
metallic = unreal.EditorAssetLibrary.load_asset(root + "/textures/Bomba_Metallic")
ao = unreal.EditorAssetLibrary.load_asset(root + "/textures/Bomba_Mixed_AO")
errors = []
if not mesh:
    errors.append("SM_Bomba missing")
if not material:
    errors.append("M_Bomba missing")
if mesh and material:
    slots = mesh.get_editor_property("static_materials")
    if not slots:
        errors.append("SM_Bomba has no material slots")
    elif any(slot.get_editor_property("material_interface") != material for slot in slots):
        errors.append("not every SM_Bomba slot uses M_Bomba")
    bounds = mesh.get_bounds().box_extent
    if bounds.x <= 0 or bounds.y <= 0 or bounds.z <= 0:
        errors.append("SM_Bomba bounds are invalid")
if normal:
    if not normal.get_editor_property("flip_green_channel"):
        errors.append("OpenGL normal green channel is not flipped")
    if normal.get_editor_property("srgb"):
        errors.append("normal map sRGB must be off")
if metallic and metallic.get_editor_property("srgb"):
    errors.append("metallic sRGB must be off")
if ao and ao.get_editor_property("srgb"):
    errors.append("AO sRGB must be off")

if errors:
    for error in errors:
        unreal.log_error("BOMBA_VALIDATE " + error)
    raise RuntimeError("Bomba asset validation failed")
unreal.log_warning("BOMBA_VALIDATE_OK slots=" + str(len(mesh.get_editor_property("static_materials"))))
