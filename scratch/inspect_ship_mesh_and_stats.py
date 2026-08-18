import unreal, os, glob, re

out_path = r"c:\Unreal Projects\ArtisticSW2026\scratch\ship_inspect_out.txt"
lines = []

# 1. Inspect Ship Mesh in Unreal
ship_path = "/Game/New/Ship/Mesh/SM_Ship"
ship_mesh = unreal.load_asset(ship_path)
lines.append(f"Loaded Ship Mesh: {ship_mesh}")

if ship_mesh:
    static_materials = ship_mesh.get_editor_property("static_materials")
    lines.append(f"  Total Material Slots: {len(static_materials)}")
    for i, sm in enumerate(static_materials):
        mat_name = sm.get_editor_property("material_interface").get_name() if sm.get_editor_property("material_interface") else "None"
        slot_name = sm.get_editor_property("material_slot_name")
        lines.append(f"    - Slot [{i}] '{slot_name}': {mat_name}")
    nanite_settings = ship_mesh.get_editor_property("nanite_settings")
    lines.append(f"  Nanite Enabled: {nanite_settings.get_editor_property('enabled') if nanite_settings else 'N/A'}")

with open(out_path, "w", encoding="utf-8") as f:
    f.write("\n".join(lines))

print("Wrote ship inspect to " + out_path)
