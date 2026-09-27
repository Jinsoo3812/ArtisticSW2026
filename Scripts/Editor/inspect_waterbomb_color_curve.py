import json
import os
import unreal


CURVE_PATH = "/Game/Resources_Assets/Shooter_VFXPack/Textures/Curves/C_SmokeColor_WaterBomb"
CURVE_PATHS = [
    CURVE_PATH,
    "/Game/Resources_Assets/Shooter_VFXPack/Textures/Curves/C_SmokeColor_01",
    "/Game/Resources_Assets/Shooter_VFXPack/Textures/Curves/C_Fire_01",
    "/Game/Resources_Assets/Shooter_VFXPack/Textures/Curves/C_Fire_02",
    "/Game/Resources_Assets/Shooter_VFXPack/Textures/Curves/C_Fire_03",
    "/Game/Resources_Assets/Shooter_VFXPack/Textures/Curves/C_Fire_04",
    "/Game/Resources_Assets/Shooter_VFXPack/Textures/Curves/C_Fire_05",
    "/Game/Resources_Assets/Shooter_VFXPack/Textures/Curves/C_Fire_06",
]
OUTPUT_PATH = os.path.join(unreal.Paths.project_saved_dir(), "waterbomb_color_curve.json")

curve = unreal.EditorAssetLibrary.load_asset(CURVE_PATH)
if not curve:
    raise RuntimeError("Could not load " + CURVE_PATH)

report = {
    "asset": curve.get_path_name(),
    "class": curve.get_class().get_name(),
    "api": [name for name in dir(curve) if not name.startswith("_")],
    "properties": {},
}

for prop in (
    "float_curves", "adjust_hue", "adjust_saturation", "adjust_brightness",
    "adjust_brightness_curve", "adjust_vibrance", "adjust_min_alpha", "adjust_max_alpha",
):
    try:
        value = curve.get_editor_property(prop)
        report["properties"][prop] = str(value)
        if prop == "float_curves":
            report["float_curves"] = []
            for channel_index, rich_curve in enumerate(value):
                channel = {
                    "index": channel_index,
                    "repr": str(rich_curve),
                    "api": [name for name in dir(rich_curve) if not name.startswith("_")],
                }
                for key_prop in ("keys", "pre_infinity_extrap", "post_infinity_extrap", "default_value"):
                    try:
                        key_value = rich_curve.get_editor_property(key_prop)
                        channel[key_prop] = str(key_value)
                        if key_prop == "keys":
                            channel["key_details"] = []
                            for key in key_value:
                                detail = {"repr": str(key)}
                                for field in ("time", "value", "interp_mode", "tangent_mode", "arrive_tangent", "leave_tangent"):
                                    try:
                                        detail[field] = str(key.get_editor_property(field))
                                    except Exception:
                                        pass
                                channel["key_details"].append(detail)
                    except Exception as exc:
                        channel[key_prop + "_error"] = str(exc)
                report["float_curves"].append(channel)
    except Exception as exc:
        report["properties"][prop] = {"error": str(exc)}

# Evaluate the authored linear-color curve at representative normalized times.
report["samples"] = {}
for time in (0.0, 0.1, 0.25, 0.5, 0.75, 0.9, 1.0):
    try:
        report["samples"][str(time)] = str(curve.get_linear_color_value(time))
    except Exception as exc:
        report["samples"][str(time)] = {"error": str(exc)}

report["comparison"] = {}
for path in CURVE_PATHS:
    candidate = unreal.EditorAssetLibrary.load_asset(path)
    if not candidate:
        continue
    report["comparison"][path] = {}
    for time in (0.0, 0.25, 0.5, 0.75, 1.0):
        report["comparison"][path][str(time)] = str(candidate.get_linear_color_value(time))

atlas = unreal.EditorAssetLibrary.load_asset(
    "/Game/Resources_Assets/Shooter_VFXPack/Textures/Curves/CA_VFXColors"
)
report["atlas"] = {"api": [name for name in dir(atlas) if not name.startswith("_")]}
for prop in ("gradient_curves", "texture_size", "texture_height", "texture_width"):
    try:
        value = atlas.get_editor_property(prop)
        report["atlas"][prop] = [str(x) for x in value] if isinstance(value, (list, tuple)) else str(value)
    except Exception as exc:
        report["atlas"][prop + "_error"] = str(exc)

with open(OUTPUT_PATH, "w", encoding="utf-8") as stream:
    json.dump(report, stream, ensure_ascii=False, indent=2)

unreal.log_warning("WATERBOMB_CURVE_ANALYSIS=" + OUTPUT_PATH)
