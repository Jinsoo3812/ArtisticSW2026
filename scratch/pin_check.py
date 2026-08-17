import unreal

material = unreal.load_asset("/Game/New/Water/Realistic_Water/M_Realistic_Water")
prefix = "/Game/New/Water/Realistic_Water/M_Realistic_Water.M_Realistic_Water:"

set_mat = unreal.load_object(None, f"{prefix}MaterialExpressionSetMaterialAttributes_2")
if set_mat:
    types = set_mat.get_editor_property("attribute_set_types")
    for i, t in enumerate(types):
        unreal.log_warning(f"Pin {i+1}: GUID={t}")
