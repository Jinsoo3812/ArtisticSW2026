# Ship Boss Enemy MVP

## Implemented runtime flow

1. Every `AEnemyShip` owns a disabled-by-default `UBossEncounterComponent`.
2. When enabled, the component locks its configured/attached `AStorageChest`-derived Enemy Item Box.
3. The first server interaction changes the encounter from `Waiting` to `Spawning` before any spawn work. Concurrent interactions therefore cannot spawn a second boss.
4. `AShipBossEnemy` is spawned at a combat waypoint using the live deck transform and its capsule half height.
5. The spawned boss is registered as the chest's single guard through the existing `ConfigureGuarding` API.
6. Boss death changes the encounter to `Defeated`; the existing chest guard system unlocks the collectible.
7. Host ship destruction fails the encounter and removes the boss/box with the ship.

## Combat decisions

The native `AShipBossAIController` is a fallback used only when the boss has no Behavior Tree.

- At 0-250 cm: knockback when `Cooldown.Boss.Knockback` is absent.
- At 0-280 cm: normal weapon attack while knockback is cooling down or unavailable.
- At 280-900 cm: dash first, vanish as fallback.
- Beyond 900 cm: vanish first, dash as fallback.
- `State.Boss.Busy` or `State.Attacking` prevents overlapping actions.

If a Behavior Tree is assigned, use:

- `BTT_SelectBossDestinationPoint` with `SelectionPurpose = Vanish` or `Dash`.
- `BTT_ActivateBossAbility` with the relevant ability asset tag.
- Blackboard input `TargetActor` (Object) and output `DestinationPointId` (Int).

## Point selection contract

Both mobility abilities use `UBossDeckPointSelector`.

Common MVP filters:

- point has `bCanUseInCombat`;
- point is in the player's rear half-plane;
- point is at least the minimum travel distance away;
- the destination capsule is not blocked.

Dash adds one purpose-specific check: the boss-to-point segment must pass through the target corridor and must not be blocked. Candidate choice is deterministic: closest to the player, then closest to the boss. The Blackboard stores a point ID rather than a world vector so the destination follows the moving ship.

`LinkedWaypointIds` remains untouched for the future point-to-point walking feature. Boss mobility currently searches every combat point instead of traversing graph links.

## Blueprint setup

### Boss

1. Create `BP_ShipBossEnemy` from `AShipBossEnemy`.
2. Assign the boss skeletal mesh, Anim Blueprint, health values, and sword definition.
3. The native class already equips `Item.EnemyWeapon.Sword` and grants knockback, vanish, and dash.
4. For authored Montages and tuning, create Blueprint subclasses of the three boss GA classes, replace the native entries in `StartingAbilities`, and avoid adding both native and Blueprint versions.
5. The sword weapon definition supplies the normal `GameplayAbility.BasicAttack`, damage GE, montage, and hit-scan notifies.

### Enemy Item Box and ship

1. Create `BP_EnemyItemBox` from `AEnemyItemBox` and attach it below the enemy ship deck/visual hierarchy.
2. In `BP_EnemyShip`, enable `BossEncounterComponent.bEncounterEnabled`.
3. Assign `BossClass = BP_ShipBossEnemy` and a valid `BossSpawnPointId`.
4. Assign the placed item box to `EnemyItemBox`, or leave it empty when it is an attached child; the component finds the first attached `AEnemyItemBox`.
5. Mark at least one deck waypoint as `bCanSpawn` and `bCanUseInCombat`. Mobility needs several combat points behind possible player facing directions.

## Gameplay assets still required

- boss sword `WeaponDefinition` with normal attack montage, damage GE, and hit-scan start/end events;
- knockback preparation/impact montage, impact cue, hit VFX, swing and impact SFX;
- vanish preparation montage, disappear/reappear Niagara cues, vanish SFX;
- dash windup/slash montage, trail Niagara cue, dash and impact SFX;
- optional boss health bar/encounter UI;
- retargeted animations for the selected boss skeleton.

The abilities remain functional without their optional Montages and cues, which allows network and combat logic testing before final art integration.

## Validation

- `ArtisticSW2026Editor Win64 Development` full build succeeds.
- `ArtisticSW.Enemy.BossMVP.Defaults` succeeds.
- `ArtisticSW.Enemy.BossMVP.PointMath` succeeds.
- `ArtisticSW.Enemy.BossMVP.EncounterSingleTrigger` succeeds.

The broader Enemy suite currently contains two pre-existing authoring failures unrelated to this MVP: the ranged combat subtree is missing the expected strafe-speed task order, and the cannon-only authoring test references `CannonVolley` data assets while the repository contains the differently named `DA_ES_Pattern_Cannon` asset family.
