# Enemy AI Core & Boss Integration Guide

This document is a concise integration of the shared Enemy AI architecture and the Ship Boss-specific runtime/Behavior Tree flow. It intentionally omits repeated setup details, asset lists, and non-core validation logs.

> **Source scope:** Sections describing the Boss encounter, Boss Behavior Tree/GAS boundary, Boss Blackboard, deck-point mobility, Strafe, and multiplayer feedback are verified against `BossAIBehaviorTreeSetup(1).md` and `BossEnemy_MVP_Implementation(1).md`. The general Enemy State/Perception, Dynamic Subtree, and shared movement-task sections are retained from the previous `Integrated_Enemy_Core_AI.md`; those details are not independently established by the two Boss-specific source files.

## 1. Enemy AI State & Perception

`ABaseAIController` owns the high-level AI state and current target.

### High-Level States (`EEnemyAIState`)
- `Passive`: Idle, guard, or patrol.
- `Investigating`: Move toward a perceived point of interest.
- `Combat`: Track, reposition, and attack a target.
- `Frozen`: Temporarily suspend normal actions.
- `Dead`: Terminal state.

`Combat` is a long-lived AI state. Short action states such as `State.Attacking` remain GAS-owned execution states.

### Perception Routing
- **Sight**: Valid target -> set `TargetActor` and enter `Combat`. When sight is lost, try alternate targets before returning to `Passive`.
- **Hearing**: A reported noise event -> set `PointOfInterest` and enter `Investigating`.
- **Damage**: Confirmed health loss with a valid instigator -> set the attacker as `TargetActor` and enter `Combat`.

---

## 2. Shared Behavior Tree Architecture

`BT_EnemyBase` acts as a high-level state router rather than containing every enemy behavior directly.

- Root Selector routes states such as `Dead -> Frozen -> Combat -> Investigating -> Passive`.
- Each state branch uses a Blackboard state condition with `Observer Aborts = Self`.
- `Run Behavior Dynamic` delegates the actual behavior to an injected subtree.
- `UEnemyBehaviorSet` maps Injection Tags to different subtrees so Melee, Ranged, Deck, or Boss enemies can reuse the same core routing model.

Typical shared Blackboard data:
- `State`
- `TargetActor`
- `PointOfInterest`
- `HomeLocation`
- `PatrolLocation`
- `PatrolRadius`

---

## 3. Ship Boss Encounter Lifecycle

`UBossEncounterComponent` owns the boss encounter lifecycle on `AEnemyShip`.

1. The encounter starts disabled unless explicitly enabled.
2. The configured Enemy Item Box is locked while the encounter is active.
3. The first valid server interaction transitions `Waiting -> Spawning` before spawn work, preventing duplicate boss spawns.
4. `AShipBossEnemy` spawns at the configured deck waypoint using the live ship/deck transform.
5. The boss becomes the chest's single guard through the existing guard API.
6. Boss death transitions the encounter to `Defeated`; the existing guard system unlocks the reward box.
7. Destroying the host ship fails the encounter and removes the encounter-owned boss/box with the ship.

`AShipBossAIController` is only a native fallback when no Behavior Tree is assigned.

---

## 4. Boss Behavior Tree ↔ GAS Responsibility

The Behavior Tree decides **when** an action is appropriate and prepares its inputs. The Gameplay Ability remains authoritative for the actual action.

### Behavior Tree owns
- Target/state branch selection.
- Ability availability checks through `BTD_CanActivateAbilityByTag`.
- Optional positioning such as `BTT_BossStrafe`.
- Destination preparation through `BTT_SelectBossDestinationPoint`.
- Ability activation/waiting through the Boss ability activation task.

### Gameplay Ability owns
- Commit and cooldown.
- Montage/movement execution.
- Hit detection and damage.
- Cancellation cleanup.
- Ending the ability.

Important contracts:
- `GA_BossDashSlash` and `GA_BossVanish` consume a destination prepared by the BT; they do not select it internally.
- Ability/cooldown checks in the BT are advisory. GAS remains authoritative.
- Once `UGA_BossBasicAttack` has committed, a later BT attack-range abort does not cancel the attack. Range is a start condition, while GAS hit/death cancellation rules own the committed action.
- Avoid duplicating weapon range or cooldown rules inside BT decorators when the weapon/GAS data already owns them.

### Recommended Boss Combat Branches
A compact combat subtree can prioritize:
- `DashSlash`: availability -> optional Strafe -> destination selection -> activate.
- `Vanish`: availability -> destination selection -> activate -> optional Strafe.
- `Knockback`: close-range condition -> availability -> activate -> optional Strafe.
- `BasicAttack`: attack-range condition -> availability -> optional Strafe -> activate.
- Short `Wait` fallback to avoid tight selector spinning.

---

## 5. Boss Blackboard & Destination Contract

Boss combat requires the shared target/state data plus:

- `DestinationPointId` (`Int`): selected deck waypoint consumed by Dash/Vanish.
- Initialize `DestinationPointId` to **`-1`**.
- Do not clear this int key with Blackboard `ClearValue`, because the default becomes `0`, which may be a valid waypoint ID.

A **point ID is stored instead of a world-space vector** so the selected destination continues to follow the moving ship/deck reference frame.

Keep destination selection immediately before ability activation to minimize stale prepared state.

---

## 6. Deck Point Selection & Mobility

`UBossDeckPointSelector` selects authored combat points on the moving deck.

Common point requirements include:
- enabled for combat;
- sufficient minimum travel distance;
- unblocked destination capsule;
- purpose-specific spatial rules.

Dash additionally uses path/corridor validity rules. Once committed, DashSlash freezes its HostShip-local path so gameplay movement and path/telegraph cues use the same immutable endpoints.

`LinkedWaypointIds` are reserved for future point-to-point traversal; current boss mobility searches eligible combat points directly.

### Source Consistency Note
The supplied documents contain different revisions of the exact facing relation used by mobility selection:

- `BossEnemy_MVP_Implementation`: both Dash and Vanish are described as using the target's rear half-plane, with Dash adding a target-corridor condition.
- `BossAIBehaviorTreeSetup`: DashSlash is described as ignoring target-facing front/rear relations, while Vanish uses a behind-target policy.
- The previous `Integrated_Enemy_Core_AI` describes a later-looking `Vanish V2 = In Front Of Target` rule.

Because these statements conflict, the exact current Vanish/Dash relation should be verified against the active `BTT_SelectBossDestinationPoint` / `UBossDeckPointSelector` implementation before documenting a single hardcoded rule. The stable contract is that the BT selects the destination and the GA consumes the prepared point.

---

## 7. Boss Strafe Contract

`BTT_BossStrafe` is an optional authored positioning flourish, not a cooldown fallback.

- Chooses left/right tangent once from the target relationship in ship-deck local space.
- Applies that fixed local direction through the live deck transform for a fixed duration.
- Performs no arrival test, clearance query, radius clamp, or replanning.
- Real collision remains `CharacterMovement`'s responsibility.
- Hitting a wall may shorten actual travel but does not fail or extend the task.
- Only invalid runtime ownership/components cause task failure.

This allows each ability sequence to use no Strafe, pre-Strafe, post-Strafe, or both without changing ability logic.

---

## 8. Shared Core Tasks & Movement

### `BTT_MoveToWeaponRange`
- Tracks `TargetActor` until within the weapon-defined attack range.
- Reads the active weapon/combat data instead of hardcoding range in the BT.
- Stops movement immediately on success or abort.

### Ability Activation by Tag
- Activate granted abilities by exact asset/gameplay tag rather than hardcoded ability classes.
- The server/GAS remains authoritative for whether activation actually succeeds.

### Movement Speed
Base locomotion speed and temporary GAS speed modifiers should remain separate:
- Base locomotion is selected by AI movement state/task.
- Temporary buffs modify the relevant attribute.
- A zero base speed should remain stationary even if a modifier exists, preserving Idle/Frozen intent.

---

## 9. Multiplayer Combat Feedback

Damage feedback is emitted only after authoritative health loss.

- Impact/Hit GameplayCues are triggered after confirmed damage, not merely at BT task start.
- GameplayCue replication distributes the event; each client creates Niagara/audio locally.
- Camera shakes target local player controllers rather than creating another client RPC layer.
- DashSlash telegraph/path effects that must survive late relevance are represented by duration GameplayEffects/GameplayCues tied to the same local-space path data.

---

## 10. Minimal Editor Wiring Checklist

1. Assign the intended core/root Behavior Tree and the correct Behavior Set/subtree assets.
2. Use the native `EEnemyAIState` Blackboard enum.
3. For Boss AI, provide `TargetActor`, state data, and `DestinationPointId = -1`.
4. Ensure every Boss GA has the exact tag referenced by its decorator/activation task.
5. For Dash/Vanish, select the destination immediately before activation and require the preselected destination.
6. Configure deck waypoints with safe combat/spawn flags and valid collision support.
7. Enable the ship's `BossEncounterComponent`, assign the exact Boss spawn waypoint ID, Boss class, and Enemy Item Box component.
8. Keep attack range/cooldown authority in weapon/GAS data rather than duplicating constants in BT logic.
9. In multiplayer PIE, verify one replicated hit feedback event per confirmed damage event and correct local-only camera shake behavior.
