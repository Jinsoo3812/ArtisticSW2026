import json
import re
from pathlib import Path
import unreal

report = {}
table = unreal.load_asset('/Game/Blueprints/Item/Data/ShipUpgrade/DT_ShipStat')
assert table
rows = json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))
report['ship_rows_before'] = json.loads(json.dumps(rows))
for row in rows:
    name = row['Name']
    if name == 'PlayerShip' or name.startswith('PlayerShip_'):
        tier = int(name.rsplit('_', 1)[1]) if '_' in name else 1
        row['MaxHealth'] = 3000 + (tier - 1) * 1500
assert unreal.DataTableFunctionLibrary.fill_data_table_from_json_string(table, json.dumps(rows))
assert unreal.EditorAssetLibrary.save_loaded_asset(table, False)
report['ship_rows_after'] = rows

# Transfer the complete, already authored NPC presentation onto the campaign NPC.
source = unreal.load_asset('/Game/Chae_Asset/YiSunSinContent/NPC/Blueprints/BP_YiSunSin_NPC')
target = unreal.load_asset('/Game/Campaign/Blueprints/BP_NPC_YiSunSin')
assert source and target
src = unreal.get_default_object(source.generated_class()).get_editor_property('mesh')
dst = unreal.get_default_object(target.generated_class()).get_editor_property('mesh')
mesh = src.get_editor_property('skeletal_mesh_asset')
anim = src.get_editor_property('anim_class')
assert mesh and anim
dst.set_skeletal_mesh_asset(mesh)
dst.set_anim_instance_class(anim)
for key in ('relative_location', 'relative_rotation', 'relative_scale3d'):
    dst.set_editor_property(key, src.get_editor_property(key))
unreal.BlueprintEditorLibrary.compile_blueprint(target)
assert unreal.EditorAssetLibrary.save_loaded_asset(target, False)
report['yi_mesh'] = mesh.get_path_name()
report['yi_animation'] = anim.get_path_name()

data = unreal.load_asset('/Game/Blueprints/Item/DA_ItemData')
assert data
definitions = data.get_editor_property('item_definitions')
report['repair_items_before'] = []
variants = {
    'WoodenPlank': 'MI_WoodPice01',
    'GoodWoodenPlank': 'MI_WoodPice02',
    'IronPlate': 'MT_SteelPice',
    'GoodIronPlate': 'MT_SteelPice02',
}
for tag, definition in definitions.items():
    name = str(tag).split('.')[-1].rstrip("')\" ")
    # GameplayTag string formatting differs across engine versions.
    matched = next((n for n in variants if 'ShipMaterials.' + n in str(tag.get_editor_property('tag_name'))), None)
    if not matched:
        continue
    material = unreal.load_asset('/Game/Chae_Asset/MT/wood/' + variants[matched])
    assert material
    path = '/Game/Blueprints/Item/Mesh/SM_Repair_' + matched
    item_mesh = unreal.load_asset(path)
    if not item_mesh:
        item_mesh = unreal.EditorAssetLibrary.duplicate_asset('/Game/Ship_Mesh/SM_Woodpice/StaticMeshes/SM_Woodpice', path)
    assert item_mesh
    item_mesh.set_material(0, material)
    assert unreal.EditorAssetLibrary.save_loaded_asset(item_mesh, False)
    text = definition.export_text()
    updated, count = re.subn(r'ItemMesh=[^,)]*', 'ItemMesh=\"' + item_mesh.get_path_name() + '\"', text)
    assert count == 1, text
    definition.import_text(updated)
    definitions[tag] = definition
    report['repair_items_before'].append(matched)
assert len(report['repair_items_before']) == 4, str(report)
data.set_editor_property('item_definitions', definitions)
assert unreal.EditorAssetLibrary.save_loaded_asset(data, False)
out = Path(unreal.Paths.project_saved_dir()) / 'PlaytestFixes20261031.json'
out.write_text(json.dumps(report, indent=2, default=str), encoding='utf-8')
unreal.log('PLAYTEST_FIXES_APPLIED ' + str(out))
