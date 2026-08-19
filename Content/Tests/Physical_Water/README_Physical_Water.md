# Physical Water Attempt

This folder is an isolated shader experiment. It does not modify the engine
Water plugin or the previous `Tests/Realistic_Water` attempt.

## Shader structure

- `M_PhysicalWater_Master` begins from the project's Ripple-capable Water
  master and overrides only the final surface Normal and Roughness attributes.
- No panning/tiled normal texture is used by the new surface path.
- Sixteen directional wave components are evaluated in continuous world space.
- Phase velocity follows the deep-water capillary-gravity dispersion relation:
  `omega^2 = g k + (surface_tension / density) k^3`.
- Component directions are distributed around `PW Wind Direction`; amplitudes
  respond to wind speed, fetch and capillary strength.
- Frequencies too small to resolve at the current pixel distance are removed
  from the normal and their slope variance is transferred into Roughness. This
  avoids distant shimmer instead of simply making the ocean flat.
- Existing Water WPO/Gerstner waves provide long geometric displacement.
- Existing RippleTex/ServerTime, refraction and underwater behavior are kept.

## Assets

- `Test_Physical_Water`: isolated test level.
- `M_PhysicalWater_Master`: physical spectrum shader master.
- `MI_PhysicalWater_Base`, `MI_PhysicalWater_Ocean`: instance chain.
- `Waves_PhysicalWater`: long-wave geometry preset.
- `Baseline/M_PhysicalWater_Master_Clean`: clean rollback copy from before the
  spectrum override.

## Scope and limitation

This is a finite analytic spectrum evaluated in the pixel shader, not yet a
large GPU FFT displacement simulation. It removes texture tiling and uses
physical dispersion, but geometry and surface slopes are still produced by two
systems. A later FFT implementation should generate displacement, slope and
foam from one shared spectrum/render-target set.
