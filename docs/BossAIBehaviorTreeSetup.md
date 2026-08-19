# Ship Boss Behavior Tree Setup

## Responsibility boundary

- Blackboard and Behavior Tree decide **when** an action is appropriate and prepare its inputs.
- `BTT_SelectBossDestinationPoint` selects and records `DestinationPointId`.
- `BTT_BossStrafe` is an optional positioning action that can be inserted before
  or after any individual ability. It is not a cooldown fallback and does not
  inspect ability cooldowns.
- `BTD_CanActivateAbilityByTag` checks whether the registered Gameplay Ability is currently available. This is an advisory branch condition; the GA remains authoritative.
- `BTT_ActivateBossAbility` activates the GA by asset tag, waits for the exact ability instance to finish, and clears prepared destination state.
- A boss GA owns only the atomic action: commit/cooldown, montage or movement execution, hit detection, damage, cancellation cleanup, and ending itself.
- `GA_BossDashSlash` and `GA_BossVanish` require a destination already selected by the BT. They no longer choose one internally.

## Blackboard contract

Use `BB_RogueBoss` with these keys:

| Key | Type | Writer | Reader |
| --- | --- | --- | --- |
| `TargetActor` | Object (Actor) | perception/target service | decorators and ability tasks |
| `State` | Enum or Name | encounter/state service | top-level combat branches |
| `HomeLocation` | Vector | initialization service | leash/recovery branch |
| `PatrolRadius` | Float | boss defaults or initialization | positioning policy |
| `DestinationPointId` | Int | `BTT_SelectBossDestinationPoint` | dash/vanish GA |

`DestinationPointId` must initialize to `-1`. Do not use Blackboard `ClearValue` for this int key because its default value becomes `0`, which is a valid deck point.

## Recommended tree assets

Create `BT_RogueBoss` as the root tree using `BB_RogueBoss`, and create `BT_Subtree_RogueBoss_Combat` as the reusable combat subtree using the same Blackboard contract.

```text
BT_RogueBoss
└─ Selector
   ├─ Sequence [State is Dead/Stunned]
   │  └─ Wait or recovery task
   ├─ Sequence [TargetActor is set, CanEngageTarget]
   │  └─ Run Behavior Dynamic: BT_Subtree_RogueBoss_Combat
   └─ Wait

BT_Subtree_RogueBoss_Combat
└─ Random/priority Selector
	├─ Sequence: DashSlash
	│  ├─ BTD_CanActivateAbilityByTag(GameplayAbility.Boss.DashSlash)
	│  ├─ BTT_BossStrafe (optional: before ability)
	│  ├─ BTT_SelectBossDestinationPoint(Dash policy)
	│  └─ BTT_ActivateBossAbility(DashSlash, RequirePreselectedDestination=true)
	├─ Sequence: Vanish
	│  ├─ BTD_CanActivateAbilityByTag(GameplayAbility.Boss.Vanish)
	│  ├─ BTT_SelectBossDestinationPoint(Behind-target policy)
	│  ├─ BTT_ActivateBossAbility(Vanish, RequirePreselectedDestination=true)
	│  └─ BTT_BossStrafe (optional: after ability)
	├─ Sequence: Knockback
	│  ├─ close-range decorator
	│  ├─ BTD_CanActivateAbilityByTag(GameplayAbility.Boss.Knockback)
	│  ├─ BTT_ActivateBossAbility(Knockback)
	│  └─ BTT_BossStrafe (optional: after ability)
	├─ Sequence: BasicAttack
	│  ├─ attack-range decorator
	│  ├─ BTD_CanActivateAbilityByTag(GameplayAbility.BasicAttack)
	│  ├─ BTT_BossStrafe (optional: before ability)
	│  └─ BTT_ActivateBossAbility(BasicAttack)
	└─ Wait(0.1–0.3 seconds)
```

The shown Strafe locations are examples, not mandatory placements. Author each
ability sequence with no Strafe, a pre-ability Strafe, a post-ability Strafe,
or both. Strafe is a timed movement flourish: walls can reduce its actual travel,
but they do not fail the task or prevent the containing sequence from continuing.

## Boss Strafe movement contract

`BTT_BossStrafe` snapshots the initial target radius in ship-deck local space
and chooses its left or right tangent once. It then applies that fixed local
direction through the live deck transform for `Strafe Duration` (0.75 seconds
by default). It performs no clearance query, destination arrival test, radius
limit, or replanning.

CharacterMovement remains responsible for real collision, so the boss may stop
or slide against a wall without failing the task. Blocked movement never extends
the duration and the task succeeds when the timer ends. Only invalid runtime
ownership (missing boss, target, host ship, deck, or movement component) fails.

DashSlash separately uses a deck-local planar acceptable range to avoid snapping
to its exact destination.

Damage feedback is not emitted by the BT task or at ability startup. The attack stamps an impact cue into its outgoing damage spec, and the damaged actor's HealthComponent executes it only after authoritative health loss.

## Multiplayer feedback

- The server does not use `GameplayCue.Boss.Attack` as damage feedback.
- The server executes weapon/ability `GameplayCue.Impact.*` only after the target's Health actually decreases.
- The server executes `GameplayCue.Boss.Hit` only after confirmed health loss, including lethal damage.
- GameplayCue replication transports the event. Each receiving client spawns Niagara/audio locally.
- Camera shakes call only local player controllers; they do not issue another client RPC.
- Attack uses scale `0.25` and radius attenuation.
- Hit uses scale `1.0`; its VFX is visible on receiving clients, while the strong shake is restricted to the local player who instigated the hit.
- Both cue Blueprints expose `Sound`, so attack/hit SFX can be assigned later without changing networking code.

## Editor wiring checklist

1. Set `BP_Ship_BossEnemy.BehaviorTree` to `BT_RogueBoss`.
2. Confirm every BPGA has the exact ability asset tag used by its decorator and task.
3. For DashSlash and Vanish, enable `RequirePreselectedDestination` on the activation task.
4. Keep destination selection immediately before activation so another branch cannot consume stale state.
5. Insert `BTT_BossStrafe` before/after only the abilities whose authored pattern needs it; do not add a global cooldown gate.
6. Keep `BTT_SetFocus(TargetActor)` active while Strafe should face the player, and use the Strafe movement-speed mode.
7. Confirm `GCN_Boss_Attack` and `GCN_Boss_Hit` are discovered under `/Game/GameplayCues`.
8. In a two-client PIE session, verify one replicated VFX per hit, weak attack shake in range, and strong hit-confirm shake only for the damaging player.
