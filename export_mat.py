import unreal

asset_path = "/Game/New/Water/Realistic_Water/M_Realistic_Water"
export_path = "C:/Unreal Projects/ArtisticSW2026/M_Realistic_Water.t3d"

try:
    task = unreal.AssetExportTask()
    task.object = unreal.EditorAssetLibrary.load_asset(asset_path)
    task.filename = export_path
    task.automated = True
    task.prompt = False
    task.options = None

    success = unreal.Exporter.run_asset_export_task(task)
    if success:
        print(f"Successfully exported to {export_path}")
    else:
        print("Failed to export.")
except Exception as e:
    print(f"Error: {e}")
