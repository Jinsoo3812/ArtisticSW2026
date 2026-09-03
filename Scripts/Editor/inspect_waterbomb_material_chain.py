import json
import os
import unreal


ROOTS = [
    "/Game/Resources_Assets/Shooter_VFXPack/Materials/MI_Projectile_WaterBomb",
    "/Game/Resources_Assets/Shooter_VFXPack/Meshes/SM_Projectile_WaterBomb",
    "/Game/Resources_Assets/Shooter_VFXPack/Particles/Emitters/NE_ProjectileHead",
]
OUTPUT = os.path.join(unreal.Paths.project_saved_dir(), "waterbomb_material_chain.json")
registry = unreal.AssetRegistryHelpers.get_asset_registry()
options = unreal.AssetRegistryDependencyOptions(
    include_soft_package_references=True,
    include_hard_package_references=True,
    include_searchable_names=True,
    include_soft_management_references=True,
    include_hard_management_references=True,
)


def safe(fn, fallback=None):
    try:
        return fn()
    except Exception as exc:
        return {"error": str(exc)} if fallback is None else fallback


def names(method, asset):
    fn = getattr(unreal.MaterialEditingLibrary, method, None)
    return safe(lambda: list(fn(asset)), []) if fn else []


queue = list(ROOTS)
seen = set()
report = {"assets": [], "material_editing_api": [x for x in dir(unreal.MaterialEditingLibrary) if "parameter" in x.lower()]}

while queue:
    path = queue.pop(0)
    package = path.split(".")[0]
    if package in seen or package.startswith("/Script/"):
        continue
    seen.add(package)
    asset = unreal.EditorAssetLibrary.load_asset(package)
    if not asset:
        continue
    cls = asset.get_class().get_name()
    deps = [str(x) for x in registry.get_dependencies(package, options)]
    entry = {"path": asset.get_path_name(), "class": cls, "dependencies": deps}

    if isinstance(asset, unreal.MaterialInstance):
        entry["parent"] = str(safe(lambda: asset.get_editor_property("parent")))
        for kind in ("scalar", "vector", "texture", "static_switch"):
            ns = names("get_{}_parameter_names".format(kind), asset)
            entry[kind + "_parameter_names"] = [str(x) for x in ns]
            getter = getattr(unreal.MaterialEditingLibrary, "get_material_instance_{}_parameter_value".format(kind), None)
            if getter:
                entry[kind + "_parameters"] = {str(n): str(safe(lambda n=n: getter(asset, n))) for n in ns}

    if isinstance(asset, unreal.Material):
        entry["expressions"] = []
        for expression in safe(lambda: unreal.MaterialEditingLibrary.get_material_expressions(asset), []):
            e = {"class": expression.get_class().get_name(), "name": expression.get_name()}
            for prop in ("parameter_name", "default_value", "constant", "texture", "function"):
                value = safe(lambda prop=prop: expression.get_editor_property(prop))
                if not isinstance(value, dict):
                    e[prop] = str(value)
            entry["expressions"].append(e)

    if isinstance(asset, unreal.StaticMesh):
        entry["materials"] = []
        for slot in safe(lambda: asset.get_editor_property("static_materials"), []):
            entry["materials"].append({
                "slot": str(safe(lambda slot=slot: slot.get_editor_property("material_slot_name"))),
                "material": str(safe(lambda slot=slot: slot.get_editor_property("material_interface"))),
            })

    report["assets"].append(entry)
    for dep in deps:
        if dep.startswith("/Game/Resources_Assets/Shooter_VFXPack/") and dep not in seen:
            queue.append(dep)

with open(OUTPUT, "w", encoding="utf-8") as stream:
    json.dump(report, stream, ensure_ascii=False, indent=2)
unreal.log_warning("WATERBOMB_MATERIAL_CHAIN=" + OUTPUT)
