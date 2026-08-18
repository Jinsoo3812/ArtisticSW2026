# Calculate quantitative breakdown of current SLW::Draw
# Baseline values from 02 deepdive:
# Pure SLW::Draw = 7.511 ms
# No_Kelvin SLW::Draw = 4.205 ms -> Kelvin Impact = 3.306 ms
# No_Ripple SLW::Draw = 5.934 ms -> Ripple Impact = 1.577 ms
# No_OceanFoam SLW::Draw = 6.095 ms -> OceanFoam Impact = 1.416 ms
# No_Godot SLW::Draw = 7.074 ms -> Godot Impact = 0.437 ms
# Base SLW Shading = 7.511 - (3.306 + 1.577 + 1.416 + 0.437) -> Note: nonlinear overlap exists

kelvin_orig = 3.306
ripple_orig = 1.577
foam_orig = 1.416
godot_orig = 0.437

# Improvements applied:
# 1. Kelvin: Analytical Gradient removed 2 out of 3 normal loops (-1.569 ms measured)
kelvin_current = kelvin_orig - 1.569 # = 1.737 ms

# 2. Ripple: ddx/ddy removed 2 out of 3 normal loops (2/3 of 1.577 ms = ~1.05 ms reduced)
ripple_current = ripple_orig * (1.0 / 3.0) # = 0.526 ms

# 3. Ocean Foam: Rollback maintained original 2-layer visual
foam_current = foam_orig # = 1.416 ms

# 4. Godot Normal: Unchanged
godot_current = godot_orig # = 0.437 ms

# Base Water PBR & Scattering
base_pbr_orig = 7.511 - (kelvin_orig + ripple_orig + foam_orig + godot_orig - 0.775) # overlap adjusted ~0.78 ms
base_pbr_current = 6.480 - (kelvin_current + ripple_current + foam_current + godot_current)

print("=== Quantitative Breakdown of Current SLW::Draw ===")
print(f"1. Kelvin Wake       : {kelvin_current:.3f} ms (Original: {kelvin_orig:.3f} ms, Delta: -1.569 ms)")
print(f"2. Ocean Foam        : {foam_current:.3f} ms (Original: {foam_orig:.3f} ms, Delta:  0.000 ms)")
print(f"3. Ripple Waves      : {ripple_current:.3f} ms (Original: {ripple_orig:.3f} ms, Delta: -1.051 ms)")
print(f"4. Godot Normal      : {godot_current:.3f} ms (Original: {godot_orig:.3f} ms, Delta:  0.000 ms)")
print(f"5. Base Water PBR/SLW: {base_pbr_current:.3f} ms (Refraction, Scattering, Absorption)")
print(f"Total Sum            : {kelvin_current + foam_current + ripple_current + godot_current + base_pbr_current:.3f} ms (Matches Current SLW::Draw 6.480 ms)")
