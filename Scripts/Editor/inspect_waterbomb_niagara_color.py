import json
import os
import unreal


ASSET_PATH = "/Game/Resources_Assets/Shooter_VFXPack/Particles/Projectiles/P_Projectile_FireAndSmoke_WaterBomb"
OUTPUT_PATH = os.path.join(unreal.Paths.project_saved_dir(), "waterbomb_niagara_color_analysis.json")


def safe(call, default=None):
    try:
        return call()
    except Exception as exc:
        return {"error": str(exc)} if default is None else default


def property_names(obj):
    result = []
    # get_editor_property requires a name; Python's exposed names are still the
    # most reliable discovery mechanism across minor UE versions.
    for name in dir(obj):
        if name.startswith("_"):
            continue
        lowered = name.lower()
        if any(token in lowered for token in ("color", "material", "render", "parameter", "emitter")):
            result.append(name)
    return result


system = unreal.EditorAssetLibrary.load_asset(ASSET_PATH)
if not system:
    raise RuntimeError("Could not load " + ASSET_PATH)

registry = unreal.AssetRegistryHelpers.get_asset_registry()
dep_options = unreal.AssetRegistryDependencyOptions(
    include_soft_package_references=True,
    include_hard_package_references=True,
    include_searchable_names=True,
    include_soft_management_references=True,
    include_hard_management_references=True,
)
dependencies = [str(x) for x in registry.get_dependencies(ASSET_PATH, dep_options)]

report = {
    "asset": system.get_path_name(),
    "class": system.get_class().get_name(),
    "system_color_related_api": property_names(system),
    "dependencies": dependencies,
    "exposed_parameters": [],
    "emitter_handles": [],
    "dependency_assets": [],
}


def inspect_object(obj):
    entry = {
        "object": str(obj),
        "class": obj.get_class().get_name() if hasattr(obj, "get_class") else type(obj).__name__,
        "api": property_names(obj),
        "properties": {},
    }
    candidates = (
        "material", "material_user_param_binding", "material_parameter_bindings",
        "color_binding", "normalized_age_binding", "renderer_visibility_tag_binding",
        "source_mode", "renderer_enabled", "sort_mode", "bindings", "parameters",
        "override_materials", "static_mesh", "mesh", "meshes", "base_material",
    )
    for prop in candidates:
        value = safe(lambda prop=prop: obj.get_editor_property(prop))
        if not isinstance(value, dict):
            entry["properties"][prop] = str(value)
    return entry

store = safe(lambda: system.get_editor_property("exposed_parameters"))
if not isinstance(store, dict):
    report["parameter_store_class"] = store.get_class().get_name() if store else None
    report["parameter_store_api"] = property_names(store) if store else []
    if store:
        for method_name in ("get_parameters", "get_user_parameters"):
            method = getattr(store, method_name, None)
            if callable(method):
                values = safe(method, [])
                report["exposed_parameters"].append({
                    "source": method_name,
                    "values": [str(value) for value in values],
                })

handles = safe(lambda: system.get_editor_property("emitter_handles"), [])
for handle in handles:
    entry = {
        "name": safe(lambda: str(handle.get_name())),
        "api": property_names(handle),
    }
    for prop in ("name", "id", "instance", "versioned_instance", "source"):
        value = safe(lambda prop=prop: handle.get_editor_property(prop))
        if not isinstance(value, dict):
            entry[prop] = str(value)
            if prop in ("instance", "versioned_instance") and value:
                entry[prop + "_class"] = value.get_class().get_name()
                entry[prop + "_api"] = property_names(value)
    report["emitter_handles"].append(entry)

extra_paths = [
    "/Game/Resources_Assets/Shooter_VFXPack/Materials/MI_Projectile",
    "/Game/Resources_Assets/Shooter_VFXPack/Materials/MI_Projectile_WaterBomb",
    "/Game/Resources_Assets/Shooter_VFXPack/Textures/Curves/C_SmokeColor_WaterBomb",
    "/Game/Resources_Assets/Shooter_VFXPack/Textures/Curves/CA_VFXColors",
]
for dep in list(dict.fromkeys(dependencies + extra_paths)):
    asset = unreal.EditorAssetLibrary.load_asset(dep)
    if not asset:
        continue
    class_name = asset.get_class().get_name()
    if "Material" in class_name or "Niagara" in class_name or "Texture" in class_name:
        entry = {
            "path": asset.get_path_name(),
            "class": class_name,
            "color_related_api": property_names(asset),
        }
        if isinstance(asset, unreal.MaterialInstance):
            entry["vector_parameter_names"] = [
                str(x) for x in safe(lambda: unreal.MaterialEditingLibrary.get_vector_parameter_names(asset), [])
            ]
            entry["scalar_parameter_names"] = [
                str(x) for x in safe(lambda: unreal.MaterialEditingLibrary.get_scalar_parameter_names(asset), [])
            ]
            entry["vector_parameters"] = {
                str(name): str(safe(lambda name=name: unreal.MaterialEditingLibrary.get_material_instance_vector_parameter_value(asset, name)))
                for name in safe(lambda: unreal.MaterialEditingLibrary.get_vector_parameter_names(asset), [])
            }
            entry["scalar_parameters"] = {
                str(name): str(safe(lambda name=name: unreal.MaterialEditingLibrary.get_material_instance_scalar_parameter_value(asset, name)))
                for name in safe(lambda: unreal.MaterialEditingLibrary.get_scalar_parameter_names(asset), [])
            }
        report["dependency_assets"].append(entry)

    if isinstance(asset, unreal.NiagaraEmitter):
        renderer_props = safe(lambda: asset.get_editor_property("renderer_properties"), [])
        entry["renderer_properties"] = [inspect_object(renderer) for renderer in renderer_props]
        editor_params = safe(lambda: asset.get_editor_property("editor_parameters"))
        if editor_params and not isinstance(editor_params, dict):
            entry["editor_parameters"] = inspect_object(editor_params)
    if isinstance(asset, unreal.StaticMesh):
        entry["static_materials"] = [
            inspect_object(slot) for slot in safe(lambda: asset.get_editor_property("static_materials"), [])
        ]

with open(OUTPUT_PATH, "w", encoding="utf-8") as stream:
    json.dump(report, stream, ensure_ascii=False, indent=2)

unreal.log_warning("WATERBOMB_COLOR_ANALYSIS=" + OUTPUT_PATH)
unreal.log_warning(json.dumps(report, ensure_ascii=False))
