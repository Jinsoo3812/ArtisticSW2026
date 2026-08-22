# Enemy Move-Speed Buff: Editor Setup and Network Test

## Runtime contract

- Behavior Trees select abilities with an exact `GameplayAbility.*` asset tag.
- `UBTT_ActivateEnemyAbilityByTag` activates a granted spec on the server and waits for that exact spec to end.
- `UGA_EnemyMoveSpeedBoost` applies a duration GameplayEffect and ends immediately.
- `UEnemyAttributeSet::MoveSpeedBonus` is replicated. Only the server resolves it into `MaxWalkSpeed`.
- Resolved speed is `(BaseMovementSpeed * SpawnMovementSpeedMultiplier) + MoveSpeedBonus`.
- A zero base speed remains zero, so Idle/Frozen logic cannot be bypassed by the buff.

## Enemy Blueprint setup

1. Optionally create a Blueprint child of `GA_EnemyMoveSpeedBoost` to tune:
   - `Move Speed Bonus`
   - `Buff Duration`
   - `Cooldown Duration`
2. Open the target Enemy Blueprint.
3. Add the native ability or its Blueprint child to `Starting Abilities`.
4. Do not write `CharacterMovement.MaxWalkSpeed` from the Blueprint. Select a base speed through `Set Base Movement Speed` or `BTT_SetMovementSpeed`.

Abilities are opt-in per Enemy archetype. An Enemy without the granted ability will fail the BT task safely.

## Behavior Tree setup

1. Add `Can Activate Ability By Tag` as an advisory decorator when cooldown-aware branch selection is needed.
2. Set `Ability Asset Tag` to `GameplayAbility.Enemy.Buff.MoveSpeed`.
3. Add `Activate Enemy Ability By Tag` as the task in that branch.
4. Set its `Ability Asset Tag` to the same tag.
5. Leave `Cancel Ability On Abort` enabled for long-running abilities. The move-speed ability ends immediately after applying its duration effect, so later BT branch aborts do not remove the already-applied buff.

The same task can activate future attack buffs, noise abilities, attacks, or utility abilities. Those abilities only need a unique asset tag and must be granted to the Enemy ASC.

Existing Boss trees may keep using `Activate Boss Ability`. It now derives from the common task and adds only Boss destination cleanup and current-weapon spec preference.

## Multiplayer PIE verification

Run PIE with one dedicated server and two clients.

1. Confirm only the server activates the ability.
2. Confirm both clients observe the Enemy moving at the boosted velocity.
3. Inspect the Enemy ASC and confirm `MoveSpeedBonus` and `State.Buff.MoveSpeed` are present during the duration.
4. Change locomotion from Jog to Run while the buff is active; the additive bonus must remain.
5. Enter Idle while the buff is active; resolved speed must be zero.
6. Let the GameplayEffect expire; speed must return to the current locomotion base speed without a BT state change.
7. Abort the BT branch while testing a long-running ability and confirm the exact active spec is cancelled.

## Automation

Relevant suites:

- `ArtisticSW.Enemy.AbilityInfrastructure`
- `ArtisticSW.Enemy.BossMVP`
- `ArtisticSW.Enemy.Network`
