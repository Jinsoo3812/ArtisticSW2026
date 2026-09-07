import copy
import json
import math
import unreal

TABLE_PATH = '/Game/Blueprints/Ship/Data/DT_ShipStat'
table = unreal.EditorAssetLibrary.load_asset(TABLE_PATH)
if not table:
    raise RuntimeError(f'Unable to load {TABLE_PATH}')

rows = json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))
base = next((row for row in rows if row.get('Name') == 'PlayerShip'), None)
if not base:
    raise RuntimeError('PlayerShip baseline row is missing')

def rounded(value):
    return int(math.floor(float(value) + 0.5))

for tier in range(2, 5):
    scale = 1.5 ** (tier - 1)
    generated = copy.deepcopy(base)
    generated['Name'] = f'PlayerShip_{tier}'
    generated['MaxHealth'] = rounded(base['MaxHealth'] * scale)
    generated['ForwardPropulsionMultiplier'] = rounded(base['ForwardPropulsionMultiplier'] * scale)
    generated['TurnTorqueMultiplier'] = rounded(base['TurnTorqueMultiplier'] * scale)
    generated['CannonDamage'] = rounded(base['CannonDamage'] * scale)
    generated['CannonFireCooldown'] = base['CannonFireCooldown'] / scale
    # Keep manual aiming and trajectory feel stable between tiers.
    generated['CannonballSpeed'] = base['CannonballSpeed']
    index = next((i for i, row in enumerate(rows) if row.get('Name') == generated['Name']), None)
    if index is None:
        rows.append(generated)
    else:
        rows[index] = generated

if not unreal.DataTableFunctionLibrary.fill_data_table_from_json_string(table, json.dumps(rows)):
    raise RuntimeError(f'Unable to update {TABLE_PATH}')
if not unreal.EditorAssetLibrary.save_loaded_asset(table, only_if_is_dirty=False):
    raise RuntimeError(f'Unable to save {TABLE_PATH}')
print('PLAYER_SHIP_TIERS_UPDATED=' + unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))
