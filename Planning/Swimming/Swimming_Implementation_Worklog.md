# Swimming implementation worklog

## Scope

This document covers character swimming only. It must not alter ship, enemy-ship, cannonball, or other physics-object buoyancy.

- Character system: `USwimmingComponent` on `ABasePlayer`.
- Ship/physics system left untouched: `USWBuoyancyComponent` in `WaterAndShip`.

## Implemented

### Vertical input

- While in `CMOVE_Swimming`, `Ctrl` holds descend and `Space` holds ascend.
- The input is sent to the server through `ABasePlayer::Server_SetSwimmingVerticalInput`.
- `IsCustomSwimming()` and `IsUnderwater()` are exposed for the animation Blueprint.
- Ctrl/Space are mutually exclusive full-body overrides: while either is held, WASD horizontal input is ignored and drag removes existing horizontal movement.
- `bDiveInputHeld` and `bAscendInputHeld` replicate from `USwimmingComponent`, so remote animation can use the same descend/ascend state.

### Underwater camera-directed movement

- When actually underwater, in `Submerged` depth mode, and neither Ctrl nor Space is held, W/A/S/D follows the full control rotation. W therefore moves along camera pitch and can rise or descend; W+A/W+D retain diagonal movement.
- Ctrl/Space do not use camera-directed movement. They own vertical travel only, which keeps the authored descend/ascend animations unambiguous.
- The animation snapshot uses total movement speed during neutral camera-directed swimming, so a steep-pitch W input still selects the forward swim loop rather than the idle loop.

### Surface and submerged movement

- `ESwimDepthMode::Surface` follows the queried wave surface using a damped height spring.
- Pressing Ctrl transitions to `ESwimDepthMode::Submerged`; releasing Ctrl does not restore surface buoyancy.
- In submerged mode, there is no upward buoyancy or gravity. Vertical velocity is damped to zero with no input, preserving the selected depth.
- A submerged character returns to surface mode only after their head clears the water surface by `SurfaceReentryHeadClearance`.
- In deep water, a floor collision stops descending but keeps the character swimming. Existing shallow-water logic still transitions to walking.

### Wave-safe surface detection

- Water height is queried from the active water body every movement update, so `Surface` mode follows the current wave height through its damped height spring.
- `IsUnderwater()` uses the character head position and two separate thresholds: `UnderwaterEntryHeadSubmersion` (default 10 cm) to enter, and `UnderwaterExitHeadClearance` (default 15 cm) to leave.
- This hysteresis keeps waves near the face from switching the submerged/surface animation every frame. It represents actual submersion; `GetDepthMode()` remains the player movement intent.

### Runtime cleanup

- Removed the legacy `FSWBuoyancyMath::SolvePontoon` solve from character swimming. It no longer affected vertical movement after the surface spring / neutral submerged model was introduced, but was still executing every swimming physics tick.
- Water-height querying, wave following, pontoon debug visualization, and the independent ship `USWBuoyancyComponent` remain unchanged.

## Enemy policy

`ABaseEnemy` does not currently use `USWCharacterMovementComponent`; it is a NavMesh/`MoveTo` ground-AI character. Adding `USwimmingComponent` to all enemies now would enter a custom movement mode that its movement component cannot simulate and would leave AI without a valid water-navigation policy.

For now, enemies remain out of scope for runtime swimming. A later enemy-swimming task should introduce a dedicated policy, with one of these approved behaviours:

1. Recover-to-surface only, then despawn/return to ship after a timeout.
2. Surface swim toward a designated recovery/boarding point using a water-aware steering task.
3. Full 3D combat swimming with a dedicated movement component, swim animations, and non-NavMesh targeting.

That work must not reuse or modify ship buoyancy.

## Editor-facing state

`USwimmingComponent::GetAnimationState()` builds the values below on the game thread. `UMotionMatchingAnimInstance` copies that snapshot into its animation proxy, so `ABP_Player` and a linked `ABP_Player_Swim` should use the following **Thread Safe Getter** nodes directly rather than an Event Graph cast/update:

- `Get Thread Safe Is Swimming`
- `Get Thread Safe Is Underwater`
- `Get Thread Safe Swim Dive Input Held`
- `Get Thread Safe Swim Ascend Input Held`
- `Get Thread Safe Swim Depth Mode`
- `Get Thread Safe Swim Speed`
- `Get Thread Safe Swim Vertical Speed`
- `Get Thread Safe Swim Direction`

The snapshot belongs to the player `USwimmingComponent`; ship buoyancy does not participate. It is copied even on frames where a distant character skips a costly motion-matching search, so remote swim state does not wait for that optimization interval. `Direction` is actor-local degrees (forward 0, right 90), `Speed` is XY magnitude, and `VerticalSpeed` is Z velocity.

Use in-place swimming loops: C++ owns character translation.

## Editor animation setup plan

- Create `ABP_Player_Swim` with the same skeleton and `UMotionMatchingAnimInstance` parent class as `ABP_Player`; implement `ALI_Player_Swim` and its `Swim` layer.
- Do not use an Event Graph for swim variables. Use `Get Thread Safe Is Swimming`, `Is Underwater`, `Swim Depth Mode`, `Swim Speed`, `Swim Vertical Speed`, and `Swim Direction` directly in the AnimGraph/state-machine rules.
- First implementation should use only `SurfaceLocomotion` and `SubmergedLocomotion` states. Transition by actual `IsUnderwater`, not Ctrl/DepthMode alone; waves are already hysteresis-filtered.
- Keep shallow water out of the swim state machine. C++ changes to `MOVE_Walking` only when feet are shallow enough and a walkable floor exists, so `IsSwimming` becomes false and the existing ground motion-matching graph automatically resumes.
- Add authored dive/surface transition sequences only after the two-state loop is verified. They must have fail-safe exits to the destination loop if a transition is interrupted by walking, falling, or rapid player input.

### Final swim animation graph

`SM_Swim` uses `SurfaceLocomotion`, `EnterDive`, `DiveToUnderwater`,
`UnderwaterLocomotion`, `Descend`, and `Ascend`. `ExitToSurface` is deliberately
not used until an authored surfacing transition exists.

- `BS_Swim_Surface`: 2D Direction (-180..180) × Swim Speed (0..300). The entire zero-speed row is the same surface idle.
- `BS_Swim_Underwater`: 2D Direction (-180..180) × Swim Speed (0..300), with forward/back/left/right and diagonal samples. In neutral underwater movement, speed is total 3D speed so W while looking steeply up/down still selects forward/directional swimming.
- `EnterDive` uses the authored surface-swim-to-dive sequence. `DiveToUnderwater`
  uses the authored dive-to-idle/underwater sequence and is only a **release/stop**
  transition; it is not the normal destination when Ctrl remains held.
- `Descend` and `Ascend` are full-body Ctrl/Space states. Since horizontal input is blocked while held, each uses a single appropriate looping vertical asset rather than directional BlendSpaces.

State conditions use the replicated Thread Safe input getters: `Dive Input Held` and `Ascend Input Held`. Actual surface/submerged changes use `Is Underwater`, never input intent alone.

### Corrected final state transitions

Use `Dive Held` for `Get Thread Safe Swim Dive Input Held`, `Ascend Held` for
`Get Thread Safe Swim Ascend Input Held`, and `Underwater` for
`Get Thread Safe Is Underwater`. `Time Ended` below means the relevant authored
transition sequence has `Time Remaining <= 0.05` (or an equivalent automatic
rule at its end).

```text
Entry -> SurfaceLocomotion

SurfaceLocomotion -> EnterDive:
    Dive Held

EnterDive -> SurfaceLocomotion:
    !Dive Held && !Underwater
EnterDive -> Descend:
    Time Ended && Underwater && Dive Held
EnterDive -> DiveToUnderwater:
    Time Ended && Underwater && !Dive Held

DiveToUnderwater -> UnderwaterLocomotion:
    Time Ended

UnderwaterLocomotion -> Descend:
    Dive Held
UnderwaterLocomotion -> Ascend:
    Ascend Held
UnderwaterLocomotion -> SurfaceLocomotion:
    !Underwater

Descend -> DiveToUnderwater:
    !Dive Held && Underwater
Descend -> Ascend:
    Ascend Held
Descend -> SurfaceLocomotion:
    !Underwater

Ascend -> UnderwaterLocomotion:
    !Ascend Held && Underwater
Ascend -> Descend:
    Dive Held
Ascend -> SurfaceLocomotion:
    !Underwater
```

Do not create `SurfaceLocomotion -> Ascend`, `UnderwaterLocomotion ->
DiveToUnderwater`, or any `Ascend -> ExitToSurface` transition. Space causes
the looping `Ascend` state only while still underwater; reaching the surface
has no authored follow-up asset, so it returns directly to the surface loop.

### ABP_Player host graph

Keep the current ground pipeline intact through its final `Pose History` node. Replace only `Pose History -> Output Pose` with:

`Pose History` -> `Blend Poses by Bool` (False Pose) -> `Output Pose`  
`Swim` animation-layer output -> `Blend Poses by Bool` (True Pose)  
`Get Thread Safe Is Swimming` -> `Blend Poses by Bool` (Active Value)

## Native linked-layer binding

`ABasePlayer` now owns the runtime `Link Anim Class Layers` call. In `BP_Player`
set **Animation > Swimming > Swimming Anim Layer Class** to `ABP_Swim`; no
`BeginPlay` Blueprint graph is needed. At BeginPlay, the player obtains its mesh
animation instance and binds all layers implemented by that class. This is run
on every spawned player instance, including simulated proxies, so the visual
layer is available on clients without a replicated Blueprint event.

Both the main player ABP and `ABP_Swim` must still implement
`ALI_Player_Swim`, and `ABP_Swim`'s `Swim` layer must connect `SM_Swim` to its
layer `Output Pose`. A missing class assignment or an empty layer output is a
reference pose (T-pose) when `Is Swimming` selects the True branch.

Use an initial blend time around 0.15–0.20 seconds and enable reset-on-activation for the linked child if available. Do not insert the swim layer before ground foot placement, leg IK, weapon overlay, or pose history; those are ground-only post-processes.

## Verification

- Compile `ArtisticSW2026Editor Win64 Development`.
- PIE: enter water, descend with Ctrl, release at depth and verify depth hold, ascend with Space, then verify wave-surface follow after surfacing.
- Two-client PIE: verify the remote character receives the same movement and surface/submerged transition.
- Test at a wave crest and trough: hover near the face-level waterline and verify `IsUnderwater()` changes only after crossing the entry/exit clearance, without flickering.
