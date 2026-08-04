import unreal
import os
import re

def check_engine_material():
    out_dir = r"C:\Unreal Projects\ArtisticSW2026\Tools\ExportedMaterials"
    if not os.path.exists(out_dir):
        os.makedirs(out_dir)

    # Let's load the default Engine Water Material
    paths = [
        "/Water/Materials/Water_Material",
        "/Water/Materials/Water_Material_Ocean"
    ]
    
    for p in paths:
        asset = unreal.EditorAssetLibrary.load_asset(p)
        if asset:
            t3d_path = os.path.join(out_dir, p.split("/")[-1] + "_Engine.t3d")
            task = unreal.AssetExportTask()
            task.object = asset
            task.filename = t3d_path
            task.automated = True
            task.prompt = False
            task.replace_identical = True
            unreal.Exporter.run_asset_export_task(task)
            
            # Read and search for Enable Ocean Foam
            with open(t3d_path, 'r', encoding='utf-8', errors='ignore') as f:
                content = f.read()
            
            # Check for StaticSwitchParameter named "Enable Ocean Foam"
            has_foam_switch = "Enable Ocean Foam" in content
            print(f"Material {p} has 'Enable Ocean Foam': {has_foam_switch}")
        else:
            print(f"Failed to load {p}")

check_engine_material()
