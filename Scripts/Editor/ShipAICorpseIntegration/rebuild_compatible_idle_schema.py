"""Re-author the exact exported schema settings using this project's UE 5.7 classes."""
import json
from pathlib import Path
import unreal
ROOT=Path(unreal.Paths.project_dir()).resolve();OUT=ROOT/'Saved/AssetRepair'
data=json.loads((OUT/'original_idle_schema.json').read_text(encoding='utf8'))
package=data['package'];path,name=package.rsplit('/',1)
factory=unreal.DataAssetFactory();factory.set_editor_property('data_asset_class',unreal.PoseSearchSchema)
schema=unreal.load_asset(package) if unreal.EditorAssetLibrary.does_asset_exist(package) else unreal.AssetToolsHelpers.get_asset_tools().create_asset(name,path,unreal.PoseSearchSchema,factory)
assert schema
def assign(obj,name,value):
    current=obj.get_editor_property(name)
    if hasattr(current,'value'):
        values=[getattr(type(current),a) for a in dir(type(current)) if getattr(getattr(type(current),a),'value',None)==value]
        assert values,(name,value);value=values[0]
    obj.set_editor_property(name,value)
for key,value in data['properties'].items():assign(schema,key,value)
skeletons=[]
for text in data['skeletons']:
    item=unreal.PoseSearchRoledSkeleton();item.import_text(text);skeletons.append(item)
schema.set_editor_property('skeletons',skeletons)
channels=[]
for row in data['channels']:
    channel=unreal.new_object(getattr(unreal,row['class']),outer=schema)
    for key,value in row['properties'].items():assign(channel,key,value)
    bones=[]
    for text in row['sampled_bones']:
        bone=unreal.PoseSearchBone();bone.import_text(text);bones.append(bone)
    channel.set_editor_property('sampled_bones',bones);channels.append(channel)
schema.set_editor_property('channels',channels)
assert unreal.EditorAssetLibrary.save_loaded_asset(schema,False)
assert [s.export_text() for s in schema.get_editor_property('skeletons')]==data['skeletons']
for channel,row in zip(schema.get_editor_property('channels'),data['channels']):
    assert [b.export_text() for b in channel.get_editor_property('sampled_bones')]==row['sampled_bones']
database=unreal.load_asset(package.replace('/PSS_SM_','/PSD_SM_'));assert database
assert database.get_editor_property('schema')==schema
report={'passed':True,'source_settings':data,'schema':schema.get_path_name(),'database':database.get_path_name(),'method':'exact settings exported in isolated UE5.8 and recreated/saved with UE5.7; no binary downgrade'}
(OUT/'schema_repair.json').write_text(json.dumps(report,indent=2),encoding='utf8')
