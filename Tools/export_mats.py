import unreal
import os

def export_mats():
    out_dir = r"C:\Unreal Projects\ArtisticSW2026\Tools\ExportedMaterials"
    if not os.path.exists(out_dir):
        os.makedirs(out_dir)

    asset_registry = unreal.AssetRegistryHelpers.get_asset_registry()
    asset_registry.wait_for_completion()

    assets = asset_registry.get_assets_by_path("/Game/Tests/Realistic_Water", recursive=True)
    
    results = []
    
    for asset_data in assets:
        path = str(asset_data.package_name)
        if any(v in path for v in ["V3_", "V4_", "V5_", "V6_"]):
            if "_M_" in path or "_MI_" in path or "_MPC_" in path:
                asset = unreal.EditorAssetLibrary.load_asset(path)
                if asset:
                    task = unreal.AssetExportTask()
                    task.object = asset
                    task.filename = os.path.join(out_dir, path.split("/")[-1] + ".t3d")
                    task.automated = True
                    task.prompt = False
                    task.replace_identical = True
                    unreal.Exporter.run_asset_export_task(task)
                    results.append(path)
                    
    with open(os.path.join(out_dir, "export_log.txt"), "w") as f:
        f.write("\n".join(results))

try:
    export_mats()
except Exception as e:
    out_dir = r"C:\Unreal Projects\ArtisticSW2026\Tools\ExportedMaterials"
    if not os.path.exists(out_dir):
        os.makedirs(out_dir)
    with open(os.path.join(out_dir, "error.txt"), "w") as f:
        f.write(str(e))
