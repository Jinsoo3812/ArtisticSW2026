# Swimming implementation worklog

## Scope

This document covers character swimming only. It must not alter ship, enemy-ship, cannonball, or other physics-object buoyancy.

- Character system: `USwimmingComponent` on `ABasePlayer`.
- Ship/physics system left untouched: `USWBuoyancyComponent` in `WaterAndShip`.

## Implemented

### Vertical input and state ownership

- `ESwimMovementState` explicitly separates `Surface`, `DiveTransition`, `Submerged`, and `SurfaceTransition`.
- A Ctrl press in `Surface` starts a 1.5 s synthetic dive. Releasing the key does not cancel it. Both transition states own vertical input and reject WASD/Ctrl/Space while preserving Look.
- `Surface` ignores Space. `Submerged` alone consumes raw Ctrl/Space input. Effective dive/ascend animation commands replicate independently of the physical key state.
- CMC saved moves preserve raw input, movement state, transition timers, stall time, entry hold time, and progress depth for replay.

### Underwater camera-directed movement

- When actually underwater, in `Submerged`, and neither Ctrl nor Space is held, W/A/S/D follows the full control rotation. W therefore moves along camera pitch and can rise or descend; W+A/W+D retain diagonal movement.
- Ctrl/Space do not use camera-directed movement. They own vertical travel only, which keeps the authored descend/ascend animations unambiguous.
- The animation snapshot uses total movement speed during neutral camera-directed swimming, so a steep-pitch W input still selects the forward swim loop rather than the idle loop.

### Surface and submerged movement

- `Surface` uses a critically damped, bidirectional PD controller targeting `SurfaceZ - SurfaceTargetDepth`; gravity and spherical pontoon buoyancy are not applied.
- `DiveTransition`, `Submerged`, and `SurfaceTransition` have no buoyancy, gravity, or Surface PD. `Submerged` damps neutral Z velocity.
- A dive that fails to reach 150 cm center depth enters automatic recovery. A submerged character shallower than 150 cm for 0.10 s enters `SurfaceTransition`.
- Automatic recovery targets 50 cm center depth and returns control on completion, a 1 s blocking stall, or the 5 s absolute timeout.
- In deep water, a floor collision stops descending but keeps the character swimming. Existing shallow-water logic still transitions to walking.

### Wave-safe surface detection

- One atomic `FSwimWaterSurfaceSample` supplies validity, surface Z, vertical velocity, normal, and selected WaterBody for the frame.
- Query validity no longer means that an implicitly lowered probe is wet. Feet, actor center, and head presentation explicitly compare their Z against the same actor-XY sample.
- `IsUnderwater()` retains head-height hysteresis for presentation only; `GetMovementState()` is authoritative for locomotion.

### Runtime cleanup

- Removed the legacy `FSWBuoyancyMath::SolvePontoon`, speed-variable pontoon reference point, Space ceiling, and separate head WaterBody query from character movement.
- Swimming enables SpringArm location lag only for the locally controlled pawn and ramps it out over 0.25 s without delaying ControlRotation.
- The independent ship/chest `USWBuoyancyComponent` path remains unchanged.

## Enemy policy

`ABaseEnemy` does not currently use `USWCharacterMovementComponent`; it is a NavMesh/`MoveTo` ground-AI character. Adding `USwimmingComponent` to all enemies now would enter a custom movement mode that its movement component cannot simulate and would leave AI without a valid water-navigation policy.

For now, enemies remain out of scope for runtime swimming. A later enemy-swimming task should introduce a dedicated policy, with one of these approved behaviours:

1. Recover-to-surface only, then despawn/return to ship after a timeout.
2. Surface swim toward a designated recovery/boarding point using a water-aware steering task.
3. Full 3D combat swimming with a dedicated movement component, swim animations, and non-NavMesh targeting.

That work must not reuse or modify ship buoyancy.

## Animation compatibility boundary

Animation source, Animation Blueprints, linked layers, and assets are not part of this implementation. The existing animation-facing contract remains unchanged: `ESwimDepthMode`, `FSwimmingAnimationState::DepthMode`, and `GetThreadSafeSwimDepthMode()` continue to expose the two values `Surface` and `Submerged`.

The four-state `ESwimMovementState` is movement-only. `USwimmingComponent` maps only `Surface` to animation `Surface`; all three other movement states map to animation `Submerged`. It also supplies the existing input booleans as effective animation commands:

- `Surface`: neither command.
- `DiveTransition`: dive command.
- `Submerged`: only allowed raw Ctrl/Space commands.
- `SurfaceTransition`: ascend command.

No Animation enum pin, getter, source file, or asset change is required. C++ owns translation, transition duration, input suppression, and surface following.

## Transition input suppression

Space pressed during `DiveTransition`, and Ctrl/Space pressed or already held when entering `SurfaceTransition`, are suppressed until their corresponding key is released. Surface Space follows the same release-before-repress rule. These physical-held and suppression states are saved and restored with CMC prediction so a transition-time press cannot fire late after replay or after entering `Submerged`.

Use `p.SwimTransitionDebug 1` to inspect the four-state movement state, raw/effective input, water sample, signed depth, target depth, vertical velocities, transition/stall timers, and blocking-hit flag.

## Verification

- Compile `ArtisticSW2026Editor Win64 Development`.
- PIE: enter water, descend with Ctrl, release at depth and verify depth hold, ascend with Space, then verify wave-surface follow after surfacing.
- Two-client PIE: verify the remote character receives the same movement and surface/submerged transition.
- During each transition, press and hold the opposite vertical key; verify it remains inactive after the transition until released and pressed again.
- Test at a wave crest and trough: hover near the face-level waterline and verify `IsUnderwater()` changes only after crossing the entry/exit clearance, without flickering.
