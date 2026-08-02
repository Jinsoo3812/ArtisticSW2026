# V5 Persistent Advected Foam

V5 is an isolated deep-ocean visual test. V4 and the project compatibility
materials remain unchanged.

Scope:

- Generate foam only from the engine's exact Gerstner WPO crest and slope.
- Store foam density in two local 2D render targets (ping-pong state).
- Backtrace the previous state with exact per-frame Gerstner horizontal
  displacement, then apply exponential lifetime decay and source injection.
- Render the persistent density as bright dense cores, broken filament edges,
  and mip-stable multi-scale detail.

The state layout and update order follow the public Fluid Flux documentation:
2D render-target state, iterative velocity/depth-derived updates, foam
advection, and surface detail advection. This version does not reproduce or
claim access to Fluid Flux implementation code.

Deliberately excluded:

- whirlpool skill flow
- rocks/coastline collision foam
- ship wakes
- shallow-water terrain simulation
- gameplay physics or replication changes

Runtime owner: `ASWPersistentFoamField`. It is a client-local visual actor and
does not change buoyancy, Water collision, Ripple replication, or server state.

Ready-to-run levels:

- `V5_Test_RealisticWater`: the V4 test layout upgraded to the persistent field.
- `V5_Showcase_PersistentFoam`: the V4 showcase layout upgraded to the same field.

Both levels use `V5_Waves_RealisticWater`, retain the `SWRippleWaterWaves`
wrapper, and contain exactly one `ASWPersistentFoamField`. The actor is inserted
by a direct editor-world C++ spawn helper because viewport ActorFactory placement
is not valid in the headless setup commandlet.
