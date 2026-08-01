# V2 - Flow-Coupled Surface

Active master: `V2_M_RealisticWater_FlowMaster_B`

`V2_M_RealisticWater_FlowMaster` is the retained Prototype A diagnostic asset.
It failed material compilation because its Custom node returned float4 data to
float2 inputs. Nothing in V2 or the active compatibility assets references it.

V2 removes the visible normal-map Panners. Macro, meso, and micro normals use
dual-phase flow advection: two reset phases cross-fade so the surface detail
continually reforms instead of sliding forever.

The flow vector combines the dominant water direction, the slope derived from
the displaced world-position surface, and a slowly evolving irregular field.
Slope also controls normal intensity. Geometry curvature and slope drive the
low-roughness reflection facets, replacing V1's visibly attached glint texture.

The existing Single Layer Water optics, Ripple wrapper, WPO, Gerstner waves,
collision, buoyancy, and gameplay physics are preserved.
