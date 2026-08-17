import unreal

asset_path = "/Game/New/Water/Realistic_Water/M_Realistic_Water_Ocean"
inst = unreal.load_asset(asset_path)

if not inst:
    unreal.log_error(f"Failed to load {asset_path}")
else:
    unreal.log_warning(f"=== Parameters for {asset_path} ===")
    
    # Scalar parameters
    for sp in inst.get_editor_property('scalar_parameter_values'):
        pinfo = sp.get_editor_property('parameter_info')
        pname = pinfo.get_editor_property('name')
        pval = sp.get_editor_property('parameter_value')
        unreal.log_warning(f"  Scalar: {pname} = {pval}")
        
    # Vector parameters
    for vp in inst.get_editor_property('vector_parameter_values'):
        pinfo = vp.get_editor_property('parameter_info')
        pname = pinfo.get_editor_property('name')
        pval = vp.get_editor_property('parameter_value')
        unreal.log_warning(f"  Vector: {pname} = ({pval.r:.2f}, {pval.g:.2f}, {pval.b:.2f}, {pval.a:.2f})")
        
    # Texture parameters
    for tp in inst.get_editor_property('texture_parameter_values'):
        pinfo = tp.get_editor_property('parameter_info')
        pname = pinfo.get_editor_property('name')
        pval = tp.get_editor_property('parameter_value')
        unreal.log_warning(f"  Texture: {pname} = {pval.get_name() if pval else 'None'}")
