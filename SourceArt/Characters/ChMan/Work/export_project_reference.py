import unreal, os, json, traceback
OUT = r'C:\Users\I\Documents\GitHub\ArtisticSW2026\SourceArt\Characters\ChMan\Reference'
os.makedirs(OUT, exist_ok=True)
report = {}
try:
    paths = {
        'Manny': '/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple',
        'ChManCurrent': '/Game/jiwon/Characters/ChManNewRig',
    }
    for label, path in paths.items():
        mesh = unreal.load_asset(path)
        if not mesh:
            raise RuntimeError('Missing ' + path)
        task = unreal.AssetExportTask()
        task.object = mesh
        task.filename = os.path.join(OUT, label + '.fbx')
        task.automated = True
        task.prompt = False
        task.replace_identical = True
        task.exporter = unreal.SkeletalMeshExporterFBX()
        task.options = unreal.FbxExportOption()
        ok = unreal.Exporter.run_asset_export_task(task)
        report[label] = {'asset': path, 'skeleton': mesh.get_editor_property('skeleton').get_path_name(), 'exported': ok}
    report['engine'] = unreal.SystemLibrary.get_engine_version()
except Exception:
    report['error'] = traceback.format_exc()
with open(os.path.join(OUT, 'project_reference.json'), 'w', encoding='utf-8') as f:
    json.dump(report, f, indent=2)
