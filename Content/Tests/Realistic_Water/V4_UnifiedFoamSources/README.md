# V4 Unified Foam Sources

Status: render-verified isolated candidate. V3 remains unchanged and the
project-level active material is not promoted automatically.

V4 reads the engine ComputeGerstnerWaves WPO directly and isolates gameplay
Ripple displacement from the final combined WPO. The Godot Water_UV flow field
is sampled at two scales and reduced to narrow contours; it only breaks the
physical mask into irregular filaments and does not decide where foam exists.

Implemented sources:

- Gerstner whitecaps: positive crest height plus normalized surface steepness.
- Ripple/impact foam: absolute displacement plus steepness or curvature.
- Ship source: reserved extension point. Current ship code emits no wake field,
  so V4 deliberately does not alter networked gameplay to fake it.

This version is stateless. D3D12 renders verify Gerstner whitecaps and irregular
Ripple rings. Persistence, advection in a render target, ship wake injection and
exponential decay belong in V5 after this source mask has passed visual review.
