import json
from pathlib import Path
import unreal

data = unreal.load_asset('/Game/Blueprints/Item/DA_ItemData')
meshes = {}
for tag, definition in data.get_editor_property('item_definitions').items():
    name = str(tag.get_editor_property('tag_name'))
    if name.startswith('Item.Id.Material.ShipMaterials.'):
        mesh = definition.get_editor_property('item_mesh')
        if mesh:
            meshes[name] = mesh.get_path_name()
for name in ('WoodenPlank', 'GoodWoodenPlank', 'IronPlate', 'GoodIronPlate'):
    assert meshes['Item.Id.Material.ShipMaterials.' + name].endswith('SM_Repair_' + name + '.SM_Repair_' + name)
table = unreal.load_asset('/Game/Blueprints/Item/Data/ShipUpgrade/DT_ShipStat')
rows = json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))
tiers = [row['MaxHealth'] for row in rows if row['Name'].startswith('PlayerShip')]
assert tiers == [3000, 4500, 6000, 7500, 9000], tiers
bp = unreal.load_asset('/Game/Campaign/Blueprints/BP_NPC_YiSunSin')
component = unreal.get_default_object(bp.generated_class()).get_editor_property('mesh')
assert component.get_editor_property('skeletal_mesh_asset').get_name() == 'SK_YiSunSin_NPC_Sash'
assert component.get_editor_property('anim_class').get_name() == 'ABP_YiSunSin_NPC_Sash_C'
report = {'repair_meshes': meshes, 'ship_health_tiers': tiers, 'yi_mesh_and_animation': 'verified'}
(Path(unreal.Paths.project_saved_dir()) / 'PlaytestAssetVerification20261031.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
unreal.log('PLAYTEST_ASSET_VERIFICATION_PASSED')
