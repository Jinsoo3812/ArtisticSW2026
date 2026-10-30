"""Read-only export of PR 121's character and authored animation references."""
import json
from pathlib import Path
import unreal

ROOT = Path(unreal.Paths.project_dir()).resolve()
OUT = ROOT / "SourceArt/Characters/YiSunSin/Reference"
OUT.mkdir(parents=True, exist_ok=True)
BASE = "/Game/Chae_Asset/YiSunSinContent"
mesh = unreal.load_asset(BASE + "/Characters/SK_YiSunSin_Latest")
if not isinstance(mesh, unreal.SkeletalMesh):
    raise RuntimeError("Character mesh did not load")
report = {"engine": unreal.SystemLibrary.get_engine_version(), "mesh": mesh.get_path_name(),
          "skeleton": mesh.get_editor_property("skeleton").get_path_name(),
          "materials": [], "exports": []}
for material in mesh.get_editor_property("materials"):
    interface = material.get_editor_property("material_interface")
    report["materials"].append({"slot": str(material.get_editor_property("material_slot_name")),
                                "material": interface.get_path_name() if interface else None})
targets = [(mesh, unreal.SkeletalMeshExporterFBX())]
for name in ["Idle", "Walk", "Run", "Jump"]:
    anim = unreal.load_asset(BASE + "/Animations/Latest/AS_YiSunSin_" + name)
    if not isinstance(anim, unreal.AnimSequence):
        raise RuntimeError("Animation did not load: " + name)
    targets.append((anim, unreal.AnimSequenceExporterFBX()))
for asset, exporter in targets:
    destination = OUT / (asset.get_name() + ".fbx")
    task = unreal.AssetExportTask()
    task.object = asset
    task.exporter = exporter
    task.filename = str(destination)
    task.automated = True
    task.prompt = False
    task.replace_identical = True
    options = unreal.FbxExportOption()
    options.set_editor_property("bake_material_inputs", unreal.FbxMaterialBakeMode.DISABLED)
    options.set_editor_property("level_of_detail", False)
    options.set_editor_property("export_morph_targets", True)
    options.set_editor_property("export_preview_mesh", False)
    task.options = options
    success = unreal.Exporter.run_asset_export_task(task)
    if not success or not destination.exists():
        raise RuntimeError("Export failed: " + asset.get_path_name())
    report["exports"].append({"asset": asset.get_path_name(), "file": destination.name,
                               "bytes": destination.stat().st_size,
                               "exporter": exporter.get_class().get_name()})
(OUT / "export_manifest.json").write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
unreal.log("YISUNSIN_REFERENCE_EXPORT_COMPLETE")
