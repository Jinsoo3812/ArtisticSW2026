# Damage feedback and animated hurtboxes

## Damage feedback responsibility

`FSWGameplayEffectContext` carries `ESWDamageDeliveryType` so health feedback does not infer intent from damage magnitude or GameplayEffect period.

- `DirectHit`: target hit cue, attack impact cue, and HitReaction event are allowed.
- `StatusTick`: direct-hit feedback and HitReaction are suppressed. Only the status GameplayEffect's `Periodic Damage Cue Tag` is executed.
- `Environment`: no character hit feedback is emitted unless a separate environment presentation policy is added.
- `Unspecified`: no target hit feedback is emitted. A legacy `GameplayCue.Impact.*` asset tag is treated as an explicit direct hit for compatibility.

Strength attacks and `CombatHitResolver` results are classified as `DirectHit`. Status effects applied through `StatusEffectLibrary` are classified as `StatusTick`, even when their context was copied from the original weapon hit. Traps are classified as `Environment`.

### Editor setup for status tick feedback

1. Create or open a GameplayCue Notify asset for the tick effect. Use `SW Gameplay Cue Burst Feedback` when a one-shot Niagara or sound effect is sufficient.
2. Give the asset the matching tag, such as `GameplayCue.Status.Poison.Tick` or `GameplayCue.Status.Burn.Tick`.
3. Open the poison or burn GameplayEffect Blueprint derived from `PoisonStatusGameplayEffect` or `BurnStatusGameplayEffect`.
4. Set `Status > Feedback > Periodic Damage Cue Tag` to the GameplayCue tag. The native poison and burn classes already provide the tags above as defaults.
5. Keep impact particles and camera shake in `GameplayCue.Impact.*`; keep status tick feedback small and attached to the target.

## Animated combat hurtbox responsibility

The character capsule remains responsible for movement and navigation. The skeletal mesh PhysicsAsset is responsible for weapon hit location and bone identity.

`UCombatHurtboxComponent` owns collision setup, authored-data validation and direct-hit surface checks. `BaseCharacter` initializes it at BeginPlay. Choose `MovementCapsule` for capsule damage or `AnimatedPhysicsAsset` for bone-based damage on the character's CombatHurtboxComponent. Bosses default to `AnimatedPhysicsAsset`; ordinary characters default to `MovementCapsule`.

Animated mode is fail-closed: missing mesh/Physics Asset, an invalid profile, disabled mesh queries, wrong object type or unavailable server bone updates never enables capsule damage. `InitializationFailure` records setup failures. The resolver and arrow collision adapter use the same component policy. A hit must identify the owner's mesh and an enabled Physics Asset body bone.

### Character editor setup

1. Restart the editor after collision configuration or native component changes.
2. Select the character Blueprint's `CombatHurtboxComponent` and set `Mode` to `AnimatedPhysicsAsset`.
3. Keep `Profile Name` set to `CharacterHurtbox` (query-only, CombatHurtbox object type, Arrow and WeaponAim Block).
4. Assign a Physics Asset containing enabled bodies on valid mesh bones. Use Blueprint data validation to check the configuration.
5. Runtime setup makes the capsule ignore Arrow and WeaponAim without changing its movement collision mode. Authority refreshes bones offscreen and disables update-rate optimization.
6. Size bodies in the Physics Asset editor against the relevant animation poses.

The authored boss uses `/Game/Fab/Samurai/UE_Samurai/SKM_UE_Samurai`. Its previously missing Physics Asset is now `/Game/Fab/Samurai/SKM_Samurai_PhysicsAsset`; the Blueprint also explicitly overrides that asset. `Scripts/configure_boss_hurtbox.py` reproduces this repair without replacing its mesh or animations.

Unlike the previous boolean setting, missing Physics Assets no longer fall back to capsule hits in animated mode. Choose `MovementCapsule` explicitly when that is the intended damage surface.

### Weapon editor setup

Melee weapon classes expose `Include Animated Combat Hurtboxes` under their Trace category. Keep it enabled for pose-accurate attacks. The authored `Trace Object Types` array remains available for additional targets; the shared hurtbox channel is added at runtime by the enabled option.

When this option is enabled, the shared hit resolver checks the target hurtbox policy. Animated mode rejects capsule hits even if initialization failed. Only valid Physics Asset body hits are accepted. DashSlash movement damage does not request this filtering and continues to use its own swept sphere volume.

### Verification

- Direct sword, arrow, and DashSlash damage emits direct-hit feedback once.
- Poison and burn ticks do not emit target hit VFX or HitReaction; their configured tick cues still execute.
- A sword trace that intersects only the Samurai capsule during Windup does not damage it.
- A sword or arrow that intersects a Samurai PhysicsAsset body reports the skeletal mesh, correct impact point, and bone name.
- DashSlash damage volume is disabled during Windup and enabled only when the dash begins.
- Dedicated-server tests or multiplayer PIE confirm that the PhysicsAsset follows the montage pose.


### Boss Capsule regression result

The Win64 Development Editor build and four focused automation tests passed: BossHurtbox.BlueprintSurfacePolicy, BossMVP.DashSlashServerPhases, GAS.Combat.AnimatedHurtboxPolicy, and Item.Arrow.CollisionProfile. The boss test loads the saved Blueprint, evaluates its authored Windup montage on authority, compares real PhysicsAsset and WeaponAim traces, and routes capsule/body hit events through the arrow adapter and GAS. Capsule hits preserve health and produce no confirmed feedback; the body hit decreases health and confirms feedback once. Invalid profile, disabled mesh, wrong object type, stopped bone ticking and Vanish reveal are covered. Log: Saved/BossHurtboxTests.log.

These are isolated authority-world tests. Interactive multiplayer PIE and dedicated-server/client visual verification were not run.
