import unreal
import os

def export_water_underside():
    out_dir = r"C:\Unreal Projects\ArtisticSW2026\Tools\ExportedMaterials"
    if not os.path.exists(out_dir):
        os.makedirs(out_dir)

    asset_path = "/Water/Materials/Functions/Water_Underside"
    asset = unreal.EditorAssetLibrary.load_asset(asset_path)
    if asset:
        task = unreal.AssetExportTask()
        task.object = asset
        task.filename = os.path.join(out_dir, "Water_Underside.t3d")
        task.automated = True
        task.prompt = False
        task.replace_identical = True
        unreal.Exporter.run_asset_export_task(task)
        print("Success")
    else:
        print("Failed to load asset")

export_water_underside()
