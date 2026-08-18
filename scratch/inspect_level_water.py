import unreal

world_path = "/Game/New/Water/Realistic_Water/Realistic_Water"
unreal.EditorLoadingAndSavingUtils.load_map(world_path)

out_path = r"c:\Unreal Projects\ArtisticSW2026\scratch\current_water_zone_settings.txt"
lines = []

lines.append("=== Current Water Settings in Realistic_Water ===")

actors = unreal.EditorActorSubsystem().get_all_level_actors()

for act in actors:
    cname = act.get_class().get_name()
    if "WaterZone" in cname or "WaterBody" in cname or "WaterMesh" in cname:
        lines.append(f"\n[Actor: {act.get_actor_label()}] Class: {cname}")
        for prop in [
            "render_target_resolution", "zone_extent", "tessellation_factor", "lod_scale",
            "tile_size", "far_distance_mesh_extent", "far_distance_material",
            "b_enable_local_only_tessellation", "local_tessellation_extent",
            "b_half_precision_texture", "velocity_blur_radius", "b_affects_landscape",
            "target_wave_mask_depth", "max_wave_height_offset"
        ]:
            try:
                val = act.get_editor_property(prop)
                lines.append(f"  - {prop}: {val}")
            except: pass
            
        # Check water mesh component if exists
        wmc = act.get_component_by_class(unreal.WaterMeshComponent)
        if wmc:
            lines.append(f"  [WaterMeshComponent: {wmc.get_name()}]")
            for prop in ["tessellation_factor", "lod_scale", "tile_size", "far_distance_mesh_extent", "far_distance_material", "force_collapse_density_level"]:
                try:
                    val = wmc.get_editor_property(prop)
                    lines.append(f"    - {prop}: {val}")
                except: pass

with open(out_path, "w", encoding="utf-8") as f:
    f.write("\n".join(lines))

print("Dumped current water settings to " + out_path)
