import os
import re

engine_water_dir = r"C:\Program Files\Epic Games\UE_5.7\Engine\Plugins\Experimental\Water\Source\Runtime\Public"
out_path = r"c:\Unreal Projects\ArtisticSW2026\scratch\water_engine_uproperties.txt"

lines = []

def parse_header(header_name):
    fpath = os.path.join(engine_water_dir, header_name)
    if not os.path.exists(fpath):
        lines.append(f"NOT FOUND: {fpath}")
        return
        
    with open(fpath, "r", encoding="utf-8", errors="ignore") as f:
        text = f.read()

    lines.append("="*80)
    lines.append(f"HEADER: {header_name}")
    lines.append("="*80)
    
    # Extract UPROPERTY definitions
    # Match UPROPERTY(...) \n type name [= default];
    pattern = re.compile(r'UPROPERTY\s*\((.*?)\)\s*([\w\:\<\>\*\s]+?)\s+(\w+)\s*(\=.*?)?;', re.DOTALL)
    for m in pattern.finditer(text):
        uprop_args = re.sub(r'\s+', ' ', m.group(1)).strip()
        type_name = re.sub(r'\s+', ' ', m.group(2)).strip()
        var_name = m.group(3).strip()
        default_val = m.group(4).strip() if m.group(4) else ""
        
        if "Edit" in uprop_args or "Category" in uprop_args:
            lines.append(f"[{var_name}] : {type_name} {default_val}")
            lines.append(f"   UPROPERTY({uprop_args})")

for h in [
    "WaterZoneActor.h",
    "WaterMeshComponent.h",
    "WaterBodyOceanComponent.h",
    "WaterBodyComponent.h",
    "WaterBodyActor.h"
]:
    parse_header(h)

with open(out_path, "w", encoding="utf-8") as f:
    f.write("\n".join(lines))

print(f"Dumped water uproperties to {out_path}")
