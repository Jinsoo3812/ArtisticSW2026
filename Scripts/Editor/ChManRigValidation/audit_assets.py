import unreal,json,traceback
from pathlib import Path
OUT=Path(unreal.Paths.project_saved_dir())/'ChManRigValidation'
OUT.mkdir(parents=True,exist_ok=True)
report={'assets':{},'errors':[]}
paths=['/Game/Blueprints/Player/BP_Player_Man','/Game/Blueprints/Player/BP_Player_Woman','/Game/Blueprints/Player/BP_BasePlayerTest','/Game/Anim_Logic/ABP_Player_Man','/Game/Anim_Logic/ABP_Player_Woman','/Game/jiwon/Characters/ChManNewRig','/Game/jiwon/Characters/ChManUE5/SKM_ChMan_UE5']
for path in paths:
    try:
        asset=unreal.load_asset(path)
        if not asset:raise RuntimeError('Missing '+path)
        row={'class':asset.get_class().get_name()}
        if isinstance(asset,unreal.Blueprint):
            cls=asset.generated_class();row['generated_class']=cls.get_path_name() if cls else None
            cdo=unreal.get_default_object(cls)
            if isinstance(cdo,unreal.Character):
                mesh=cdo.get_editor_property('mesh')
                row['mesh']=mesh.get_editor_property('skeletal_mesh_asset').get_path_name() if mesh.get_editor_property('skeletal_mesh_asset') else None
                row['anim_class']=mesh.get_editor_property('anim_class').get_path_name() if mesh.get_editor_property('anim_class') else None
                row['mesh_relative_transform']=mesh.get_relative_transform().export_text()
            if isinstance(asset,unreal.AnimBlueprint):
                for prop in ['target_skeleton','preview_skeletal_mesh','parent_class']:
                    try:
                        value=asset.get_editor_property(prop);row[prop]=value.get_path_name() if value else None
                    except Exception as e:row[prop+'_error']=str(e)
                for prop in ['function_graphs','ubergraph_pages']:
                    try:row[prop]=[g.get_path_name() for g in asset.get_editor_property(prop)]
                    except Exception as e:row[prop+'_error']=str(e)
                row['anim_cdo_properties']={}
                for name in ['idle_database','walk_database','run_database','sprint_database','current_active_pose_search_database']:
                    try:
                        value=cdo.get_editor_property(name);row['anim_cdo_properties'][name]=value.get_path_name() if value else None
                    except Exception:pass
                row['loaded_nodes']=[]
                for node in unreal.ObjectIterator(unreal.AnimGraphNode_Base):
                    outer=node.get_outer()
                    chain=[]
                    while outer:
                        chain.append(outer)
                        outer=outer.get_outer()
                    if asset not in chain:continue
                    item={'name':node.get_name(),'class':node.get_class().get_name(),'graph':node.get_outer().get_name()}
                    for prop in ['node','node_comment','node_guid']:
                        try:
                            value=node.get_editor_property(prop);item[prop]=value.export_text() if hasattr(value,'export_text') else str(value)
                        except Exception as e:item[prop+'_error']=str(e)
                    row['loaded_nodes'].append(item)
        if isinstance(asset,unreal.SkeletalMesh):
            row['skeleton']=asset.get_editor_property('skeleton').get_path_name()
            row['physics_asset']=asset.get_editor_property('physics_asset').get_path_name() if asset.get_editor_property('physics_asset') else None
        report['assets'][path]=row
    except Exception:report['errors'].append(traceback.format_exc())
registry=unreal.AssetRegistryHelpers.get_asset_registry()
for path in ['/Game/jiwon/Characters/ChManNewRig','/Game/Anim_Logic/ABP_Player_Man']:
    report.setdefault('referencers',{})[path]=[str(x) for x in registry.get_referencers(path,unreal.AssetRegistryDependencyOptions(include_hard_package_references=True,include_soft_package_references=True))]
report['blueprint_editor_methods']=[n for n in dir(unreal.BlueprintEditorLibrary) if not n.startswith('_')]
report['api_helpers']=[n for n in dir(unreal) if 'Object' in n and ('Iterator' in n or 'Outer' in n)]
(OUT/'asset_audit.json').write_text(json.dumps(report,indent=2,ensure_ascii=False),encoding='utf8')
