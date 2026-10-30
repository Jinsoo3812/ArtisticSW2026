import json
assets=[unreal.load_asset('/Game/Blueprints/Ship/Cannon/'+p) for p in ['BP_Cannon','DA_CannonOperatorStation','Mesh/Operator/SM_OperatorCarriageVisual','Mesh/Operator/SM_OperatorBarrelVisual']]
assert all(assets)
editors=unreal.get_editor_subsystem(unreal.AssetEditorSubsystem)
for asset in assets:editors.close_all_editors_for_asset(asset)
bp,profile,carriage,barrel=assets
for name in ['Operator_LeftPalm','Operator_RightPalm']:
 assert barrel.find_socket(name),name
 assert not carriage.find_socket(name),name
sub=unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem);fn=unreal.SubobjectDataBlueprintFunctionLibrary;tags={}
for h in sub.k2_gather_subobject_data_for_blueprint(bp):
 o=fn.get_object_for_blueprint(sub.k2_find_subobject_data_from_handle(h),bp)
 if o and o.get_name().removesuffix('_GEN_VARIABLE') in ['OperatorBarrelVisual','OperatorCarriageVisual']:
  tags[o.get_name().removesuffix('_GEN_VARIABLE')]=[str(t) for t in o.get_editor_property('component_tags')]
assert 'CannonOperatorContact' in tags['OperatorBarrelVisual'] and 'CannonOperatorContact' not in tags['OperatorCarriageVisual'],tags
result={'fresh_editor_persisted_barrel_sockets':True,'legacy_carriage_sockets_absent':True,'tags':tags,'affected_asset_editors_closed':True}
(OUT/'barrel_persistence_audit.json').write_text(json.dumps(result,indent=2))
