import unreal

for path in ["/Game/New/Ship/Blueprints/BP_PlayerShip", "/Game/Tests/Landscape/Kelvin/BP_PlayerShip_Kelvin"]:
    bp = unreal.load_asset(path)
    if bp:
        unreal.log_warning(f"=== Inspecting {path} ===")
        gen_cls = bp.generated_class()
        if gen_cls:
            cdo = unreal.get_default_object(gen_cls)
            bc = cdo.get_editor_property("sw_buoyancy_component") if hasattr(cdo, "sw_buoyancy_component") else None
            if bc:
                fs = bc.get_editor_property("force_settings")
                unreal.log_warning(f"  BuoyancyCoefficient: {fs.get_editor_property('buoyancy_coefficient')}")
                unreal.log_warning(f"  BuoyancyDamp (Linear): {fs.get_editor_property('buoyancy_damp')}")
                unreal.log_warning(f"  BuoyancyDamp2 (Quadratic): {fs.get_editor_property('buoyancy_damp2')}")
                unreal.log_warning(f"  MaxBuoyantForce: {fs.get_editor_property('max_buoyant_force')}")
                unreal.log_warning(f"  DeepWaterBuoyancyMultiplier: {fs.get_editor_property('deep_water_buoyancy_multiplier')}")
                pontoons = bc.get_editor_property("pontoons")
                unreal.log_warning(f"  Pontoons count: {len(pontoons)}")
                for idx, p in enumerate(pontoons):
                    unreal.log_warning(f"    Pontoon[{idx}]: Loc={p.get_editor_property('relative_location')} Radius={p.get_editor_property('radius')} ForceScale={p.get_editor_property('force_scale')}")
            # Also check RootComponent mass and physics damping
            root = cdo.get_editor_property("buoyancy_root") if hasattr(cdo, "buoyancy_root") else None
            if root:
                unreal.log_warning(f"  Mass (kg): {root.get_editor_property('mass_in_kg') if hasattr(root, 'mass_in_kg') else 'N/A'}")
                unreal.log_warning(f"  LinearDamping: {root.get_editor_property('linear_damping') if hasattr(root, 'linear_damping') else 'N/A'}")
                unreal.log_warning(f"  AngularDamping: {root.get_editor_property('angular_damping') if hasattr(root, 'angular_damping') else 'N/A'}")

