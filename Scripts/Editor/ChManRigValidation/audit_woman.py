import unreal,json,shutil
from pathlib import Path
root=Path(unreal.Paths.project_dir());out=root/'Saved/ChWomanRigValidation';out.mkdir(parents=True,exist_ok=True)
base=root/'SourceArt/Characters/ChWoman';(base/'Textures').mkdir(parents=True,exist_ok=True);(base/'Reference').mkdir(exist_ok=True)
lib=unreal.SWCharacterRigValidationLibrary
bp=unreal.load_asset('/Game/Blueprints/Player/BP_Player_Woman');abp=unreal.load_asset('/Game/Anim_Logic/ABP_Player_Woman')
cdo=unreal.get_default_object(bp.generated_class());component=cdo.get_editor_property('mesh');mesh=component.get_editor_property('skeletal_mesh_asset')
for relative in ['Blueprints/Player/BP_Player_Woman.uasset','Anim_Logic/ABP_Player_Woman.uasset']:
    dest=out/'Before'/relative;dest.parent.mkdir(parents=True,exist_ok=True)
    if not dest.exists():shutil.copy2(root/'Content'/relative,dest)
report={'graph':json.loads(lib.describe_graph(abp)),'mesh':json.loads(lib.describe_mesh(mesh)),
        'character_mesh':mesh.get_path_name(),'component_transform':component.get_relative_transform().export_text(),'materials':[]}
for mat_slot in mesh.get_editor_property('materials'):
    mat=mat_slot.get_editor_property('material_interface');row={'material':mat.get_path_name(),'textures':[]}
    for tex in unreal.MaterialEditingLibrary.get_used_textures(mat):
        task=unreal.AssetExportTask();task.object=tex;task.filename=str(base/'Textures'/(tex.get_name()+'.tga'));task.automated=True;task.prompt=False;task.replace_identical=True;task.exporter=unreal.TextureExporterTGA()
        row['textures'].append({'asset':tex.get_path_name(),'file':task.filename,'exported':unreal.Exporter.run_asset_export_task(task)})
    report['materials'].append(row)
report['texture_exports']=[]
for path in unreal.EditorAssetLibrary.list_assets('/Game/jiwon/Characters/ChGirlMtl/ChGirlTextureMap',True,False):
    tex=unreal.load_asset(path)
    if not isinstance(tex,unreal.Texture2D):continue
    task=unreal.AssetExportTask();task.object=tex;task.filename=str(base/'Textures'/(tex.get_name()+'.tga'));task.automated=True;task.prompt=False;task.replace_identical=True;task.exporter=unreal.TextureExporterTGA()
    report['texture_exports'].append({'asset':path,'exported':unreal.Exporter.run_asset_export_task(task)})
(out/'source_asset_audit.json').write_text(json.dumps(report,indent=2),encoding='utf8')
for label,asset in [('Quinn','/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple'),('WomanCurrent',mesh.get_path_name())]:
    task=unreal.AssetExportTask();task.object=unreal.load_asset(asset);task.filename=str(base/'Reference'/(label+'.fbx'));task.automated=True;task.prompt=False;task.replace_identical=True;task.exporter=unreal.SkeletalMeshExporterFBX();task.options=unreal.FbxExportOption()
    report[label+'_exported']=unreal.Exporter.run_asset_export_task(task)
(out/'source_asset_audit.json').write_text(json.dumps(report,indent=2),encoding='utf8')
