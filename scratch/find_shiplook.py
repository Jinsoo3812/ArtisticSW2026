import unreal

with open("c:/Unreal Projects/ArtisticSW2026/Source/WaterAndShip/Private/Ship.cpp", "r", encoding="utf-8", errors="ignore") as f:
    lines = f.readlines()

for idx, line in enumerate(lines):
    if any(k in line for k in ["ShipLook", "ShipZoom", "CameraBoom"]):
        unreal.log_warning(f"Line {idx+1}: {line.strip()}")
