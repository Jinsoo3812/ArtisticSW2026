# Realistic Water — Visual Shader

`M_RealisticWater_VisualMaster` is a visual-only branch of the Ripple-capable
V3B master. It does not replace the Water Waves asset, vertex displacement,
collision, buoyancy, underwater-volume detection, or Ripple injection.

The surface pipeline contains:

- three independent 2048 px stochastic normal spectra (macro, meso, micro),
  rotated in world space and moved at different directions and speeds;
- scale-specific distance fades and explicit Mip bias, so unresolved micro
  slopes disappear without erasing the long water shape;
- angle-corrected normal blending to avoid the energy loss of simple addition;
- stable dielectric specular (`0.255`, approximately water's `F0 ≈ 0.02`) and
  a controlled base roughness;
- sparse glint facets implemented as a small roughness reduction. Glint light
  still comes from the sun/environment reflection, not emissive color;
- distance roughness that absorbs unresolved normal variance and suppresses
  far-field sparkling/aliasing;
- the engine Single Layer Water path for depth-dependent scattering,
  absorption, reflection, refraction, and the underwater post process.

Rollback assets are preserved in `Presets`, including the clean Ripple V3B
master and the previously active V4B Soft material instances.
