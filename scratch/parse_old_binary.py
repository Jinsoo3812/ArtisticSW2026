with open(r"c:\Unreal Projects\ArtisticSW2026\scratch\M_Realistic_Water_Old.uasset", "rb") as f:
    data = f.read()

import re

for target in [b"ScaleFoam", b"WhiteFoam", b"SW_CALCULATE_FLUX_DEEP_OCEAN_FOAM"]:
    for m in re.finditer(target, data):
        start = max(0, m.start() - 300)
        end = min(len(data), m.end() + 300)
        chunk = data[start:end]
        strings = re.findall(rb'[A-Za-z0-9_]{3,}', chunk)
        print(f"\n--- Around {target} at {m.start()} ---")
        print([s.decode('latin1') for s in strings])
