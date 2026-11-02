import json
f=live_api['cannon_test'];rows=[]
sub=unreal.get_default_object(unreal.load_class(None,'/Script/Engine.SubsystemBlueprintLibrary'))
for pc in f['pcs']:
    c=pc.get_controlled_pawn();assert isinstance(c,unreal.Cannon)
    inp=sub.call_method('GetLocalPlayerSubSystemFromPlayerController',args=(pc,unreal.EnhancedInputLocalPlayerSubsystem.static_class()))
    priority=inp.has_mapping_context(c.get_editor_property('cannon_input_mapping_context'))
    assert priority is not None,'Cannon context missing'
    look=inp.query_keys_mapped_to_action(c.get_editor_property('cannon_look_action'))
    exit_keys=inp.query_keys_mapped_to_action(c.get_editor_property('cannon_exit_action'))
    assert look and exit_keys,'Cannon physical key mappings missing'
    rows.append({'controller':pc.get_path_name(),'cannon':c.get_name(),'priority':priority,'look_keys':[str(k) for k in look],'exit_keys':[str(k) for k in exit_keys]})
(OUT/'cannon_input_state.json').write_text(json.dumps(rows,indent=2));result=rows
