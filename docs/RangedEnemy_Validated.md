# Ranged Enemy Core Guide

This document summarizes the **current structural contracts** of the Ranged Enemy system:
Behavior Tree combat decisions, Bow Gameplay Ability execution, EQS repositioning,
weapon-range retreat, projectile damage, and moving-ship support.

Detailed editor click paths, tuning examples, debug steps, old prototype loops, and test commands are intentionally omitted.

---

## 1. Overall Runtime Structure

```text
Perception / Combat Target
        |
        v
BT_Subtree_RangedEnemy_Combat
        |
        +-> Emergency Retreat
        |
        +-> Ranged Attack
        |
        +-> EQS Reposition
        |
        `-> Track Target fallback
                 |
                 v
        GA_RangedEnemyAttack
                 |
                 v
        Arrow_socket -> Projectile
                 |
                 v
        Projectile DamageData -> GameplayEffect
```

Core responsibilities:

- **AI Controller / Perception**: owns the current combat target and Combat state.
- **Behavior Tree**: decides whether to retreat, attack, reposition, or track.
- **EQS**: chooses a reachable combat position with usable range and LOS.
- **`GA_RangedEnemyAttack`**: owns one atomic ranged attack.
- **Bow**: owns projectile class/speed and weapon combat data.
- **Projectile**: owns hit collision and damage application.

Movement does not use a Gameplay Ability. BT/NavMovement own positioning; GAS is used for the attack and optional movement buffs.

---

## 2. Combat Behavior Tree

The intended high-level priority is:

```text
Root Selector
├─ Emergency Retreat
├─ Ranged Attack
├─ EQS Reposition
└─ Track Target
```

### A. Emergency Retreat

Highest-priority safety branch.

```text
Target too close
-> Set Movement Speed: Run
-> optional MoveSpeed ability
-> BTT_RetreatToWeaponRange
```

Important contracts:

- close-range decorator aborts **Lower Priority**, not itself;
- the optional SpeedUp ability must not gate the retreat branch;
- base Run speed is applied before any additive speed buff;
- `BTT_RetreatToWeaponRange` moves **away** from the target and must not be replaced with the approach-oriented `BTT_MoveToWeaponRange`.

If no valid retreat NavMesh point exists, the task fails and lower-priority reposition logic can take over.

### B. Ranged Attack

```text
Can Ranged Attack
-> Set Movement Speed: Idle
-> Set Focus(TargetActor)
-> BTT_RangedAttack
-> Clear Focus
```

`BTT_RangedAttack` locates the exact Ability Spec tagged:

```text
GameplayAbility.RangedAttack
```

and waits until that Gameplay Ability finishes.

### C. EQS Reposition

Executed when the current position cannot produce a valid shot.

```text
TargetActor valid
-> Run EQS Query
-> PointOfInterest
-> Set Movement Speed: Strafe
-> Set Focus(TargetActor)
-> Move To(PointOfInterest)
-> Clear Focus
```

After movement, the next BT evaluation decides whether the enemy can now attack.

### D. Track Target

Fallback when an attack cannot start and repositioning cannot produce a usable move.

```text
TargetActor valid
-> keep focus / movement policy
-> short Wait
-> reevaluate combat
```

This prevents the Selector from spinning when EQS fails.

---

## 3. Source Revision Note: Combat Tree Layout

The supplied guides contain two Ranged combat-tree revisions.

One guide describes the explicit priority loop:

```text
Attack
-> Reposition if attack is impossible
-> Track Target if EQS movement fails
```

while the EQS editor guide shows a combined sequence:

```text
Run EQS
-> Move
-> Can Ranged Attack
-> Ranged Attack
```

The previous `Integrated_Ranged_Enemy.md` mixes both layouts: its overview lists a
standalone Attack branch, but its detailed BT section only documents the combined
`Reposition And Attack` branch.

For structural documentation, this guide uses the explicit:

```text
Retreat -> Attack -> Reposition -> Track
```

layout because it matches the Bow/GAS MVP runtime sequence and the retreat guide.
If the current `.uasset` still uses `Reposition And Attack`, the live Behavior Tree
asset should be treated as authoritative and this section updated accordingly.

Regardless of layout, the stable contract is:

- a valid shot should execute the attack GA;
- an invalid shot should trigger repositioning rather than leaving the enemy idle;
- range/LOS are revalidated immediately before the actual attack.

---

## 4. Ranged Attack Ability

`GA_RangedEnemyAttack` owns one ranged attack lifecycle on the server.

```text
Activate
-> validate target / distance / LOS
-> play attack Montage
-> wait for Event.Montage.FireArrow
-> read current Arrow_socket transform
-> spawn and launch projectile
-> end Ability
```

The server revalidates attack conditions even if the BT already checked them.

### Fire timing

Preferred path:

```text
Attack Montage
-> Event.Montage.FireArrow
-> Spawn Projectile
```

The event is placed at the bow release frame.

Fallback behavior:

- no Montage -> fire immediately;
- Montage exists but fire event is missing -> fire once when the Montage completes.

A single ability activation must spawn at most one projectile even if callbacks/events are duplicated.

---

## 5. Arrow Origin Contract

`ARangedEnemy::GetRangedAttackOrigin` uses only:

```text
Character Skeletal Mesh
-> Arrow_socket
-> World Transform
```

There is no weapon-mesh or Actor-location fallback in the refactored Bow/GAS flow.

If the socket is missing, the attack origin is invalid and the shot should fail.

The same socket basis is used for:

- final line-of-sight validation;
- actual projectile spawn position.

During an active ranged attack, the server mesh temporarily refreshes pose/bones so the release-frame socket remains current even on a server or when the character is not visually rendered. The previous mesh tick policy is restored when the ability finishes.

---

## 6. Weapon & Attack Range Contract

The equipped Bow is the primary source of maximum attack range.

```text
DA_Weapon
└─ Item.EnemyWeapon.Bow
   └─ CombatData.AttackRange
```

`ARangedEnemy.MaxAttackRange` is only a fallback when valid weapon range data is unavailable.

This same range policy should be reflected in:

- `Can Ranged Attack`;
- EQS range parameters;
- retreat destination policy.

Keeping these values inconsistent can cause EQS to select a location that still fails the final attack validation.

---

## 7. EQS Combat Position

`EQS_RangedEnemy_CombatPosition` uses:

```text
UEnvQueryContext_EnemyCombatTarget
```

The Context resolves the active combat target through the AI Controller rather than depending on a hardcoded Blackboard key name.

Conceptual query:

```text
Target-centered Donut candidates
        |
        v
valid Target distance
        |
        v
minimum movement from current position
        |
        v
reachable from Querier
        |
        v
clear candidate -> Target LOS
        |
        v
PointOfInterest
```

Core tests:

1. **Target Distance**
   - keeps candidates inside the configured combat range;
   - can score farther candidates higher.

2. **Querier Distance**
   - rejects tiny reposition moves;
   - favors nearby valid positions to reduce unnecessary cross-map movement.

3. **Pathfinding**
   - requires a real path from the current enemy position.

4. **Visibility Trace**
   - predicts whether a shot from the candidate can see the target.

`PointOfInterest` is reused as a state-dependent location:

```text
Investigating -> heard / perceived location
Combat       -> selected ranged combat position
```

Only one state subtree runs at a time, so the shared Blackboard key is intentional.

---

## 8. EQS Validation vs Final Attack Validation

EQS LOS and `Can Ranged Attack` are not duplicates.

```text
EQS
-> predicts whether a candidate position should work
-> runs before movement

Can Ranged Attack / GA
-> validates the real current position
-> uses the actual Arrow_socket
-> runs immediately before firing
```

Therefore both checks are required.

A candidate may pass EQS but still fail final attack validation because:

- the target moved;
- the ship moved;
- the character pose/socket changed;
- geometry changed;
- EQS and runtime range policies are inconsistent.

---

## 9. Retreat-to-Weapon-Range

`BTT_RetreatToWeaponRange` is a server-side emergency movement task.

Conceptually:

```text
Player too close
-> find NavMesh candidates opposite the Player
-> choose destination inside weapon maximum range
-> move away
-> repath if Player meaningfully moves
```

The destination is placed slightly inside the weapon's maximum range rather than directly on the range boundary.

If all retreat candidates fail, the task fails so the normal EQS reposition branch can try a more general combat location.

On target loss, BT abort, or death, the movement must stop immediately.

---

## 10. Bow & Projectile Responsibility

### `AEnemyBow`

Owns:

- `ProjectileClass`
- `ProjectileSpeed`
- weapon definition connection

It does not own the character's shot-origin transform.

### Ranged Projectile

Owns:

- movement;
- projectile collision;
- hit handling;
- damage data / GameplayEffect application.

On launch it must ignore at least:

- the firing enemy;
- the equipped bow;
- the relevant HostShip.

Projectile creation and damage are server authoritative.

---

## 11. Ground & Moving-Ship Support

The same Ranged Enemy combat system is intended to work:

```text
Ground
or
Moving EnemyShip deck
```

`HostShip` is optional.

On a ship:

- CharacterMovement/Based Movement keeps the enemy on the live deck;
- the enemy is not manually hard-attached as a substitute for CharacterMovement;
- LOS/projectile logic ignores the host ship where appropriate;
- shot origin is read from the **current** `Arrow_socket`, so ship translation/rotation is naturally reflected.

The Ranged Enemy should still function when no HostShip exists.

---

## 12. Minimal Integration Checklist

### Character / Weapon
- `BP_RangedEnemy` uses the Bow loadout.
- Bow grants `GA_RangedEnemyAttack`.
- character Skeleton contains `Arrow_socket`.
- Bow weapon definition provides projectile/attack-range data.

### Behavior Tree
- Emergency Retreat has highest priority.
- valid current shots can reach `BTT_RangedAttack`.
- invalid shots can reach EQS reposition logic.
- EQS failure has a Track Target fallback.

### EQS
- target-centered combat-position query is configured.
- candidates satisfy range, movement-distance, path, and LOS constraints.
- result is stored in `PointOfInterest`.

### Attack
- GA performs final target/range/LOS validation.
- release event spawns from the current `Arrow_socket`.
- one attack activation creates at most one projectile.

### Multiplayer
- AI decisions, EQS/pathing, GA activation, projectile spawn, and damage are server authoritative.
- clients receive replicated movement, Montage, projectile, and hit results.
