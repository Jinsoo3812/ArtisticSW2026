import unreal

table = unreal.EditorAssetLibrary.load_asset('/Game/Blueprints/Ship/Data/DT_ShipStat')
if not table:
    raise RuntimeError('DT_ShipStat not found')
print('SHIP_STAT_JSON=' + unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))

for tier in range(1, 5):
    path = f'/Game/Blueprints/Ship/Enemy_Ship/Data/Archetype/Normal/DA_ES_Normal_{tier}'
    asset = unreal.EditorAssetLibrary.load_asset(path)
    if asset:
        print(f'AIM_{tier}=' + str(asset.get_editor_property('cannon_aim_profile')))
