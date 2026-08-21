# V3 - Godot-Inspired Flow and Wave Basis

STATUS: RENDER-VERIFIED CANDIDATE / NOT ACTIVE

This version passes D3D12 material compilation, time-separated motion, distance,
Ripple injection, and GPU capture tests. The active compatibility level remains
on V2 until the unimplemented Foam/Caustic passes are evaluated.

This version keeps Unreal's Single Layer Water, Ripple integration, Water Body,
Gerstner-wave asset, collision, and buoyancy. It changes only the visual normal
pipeline inherited from V2.

Two ideas are adapted from UnionBytes' Godot Realistic Water demo:

1. The normal UVs are warped by a slowly changing motion field before the two
   traveling normal phases are sampled. This breaks the straight, texture-sheet
   sliding motion.
2. The final detail normal is rebuilt in the tangent frame of the actually
   displaced water surface. Large waves therefore bend the small ripples instead
   of carrying a flat normal pattern across their crests.

Source reference:
https://github.com/godot-extended-libraries/godot-realistic-water

The reference is MIT licensed. Source `Water_UV`, `Water_N_A`, `Water_N_B`,
`Foam`, and `Caustic` textures are imported under `GodotSource`. The surface
pipeline preserves the source shader's UV-motion sampling and 75:25 normal
blend, then adapts the result to Unreal's displaced wave basis and Single Layer
Water optics. Foam and caustic textures are preserved for later depth/projector
passes but are not connected to the surface shader yet.

Active master inside this version: `V3_M_RealisticWater_GodotInspired`

Levels:

- `V3_Test_RealisticWater`: original deep-ocean comparison level.
- `V3_Showcase_GodotShallow`: isolated shallow-water presentation level; only
  this copy raises the Landscape from Z=-1000 to Z=-250.
