# Boss Attacks Core Guide

This document summarizes the **structure and runtime contracts** of the Rogue Boss BasicAttack and DashSlash systems.  
Detailed editor click paths, tuning examples, troubleshooting logs, initialization scripts, and automated test commands are intentionally omitted.

## 1. Overall Responsibility Structure

```text
Behavior Tree
  ├─ decides whether/which attack can start
  ├─ prepares destination data when required
  └─ activates the Gameplay Ability by tag

Gameplay Ability
  ├─ Commit / Cooldown
  ├─ Montage and action lifecycle
  ├─ movement and hit detection
  ├─ damage request
  └─ cancellation / cleanup

GameplayEffect
  └─ authoritative damage application

GameplayCue
  ├─ ability presentation: Telegraph / Path effects
  └─ confirmed-damage feedback: Impact / Hit effects
```

- `BTT_SelectBossDestinationPoint` prepares mobility destinations before Dash/Vanish abilities start.
- `BTT_ActivateBossAbility` activates the matching GA Spec and waits for that action to finish.
- BT conditions are primarily **start conditions**. After BasicAttack or DashSlash commits, ordinary BT branch aborts do not cancel the atomic action; GAS cancellation rules such as hit/death remain authoritative.
- Damage feedback cues are not fired simply because an attack started. Impact/Hit feedback is emitted after authoritative Health loss is confirmed.

---

## 2. BasicAttack Architecture

The Boss uses the dedicated `UGA_BossBasicAttack` for `GameplayAbility.BasicAttack` while the generic weapon melee ability remains available for normal enemy compatibility.

### Shared weapon data

`DA_Weapon > Item.EnemyWeapon.Sword` owns data shared by every Boss BasicAttack:

- `AttackRange`
- `DamageEffectClass`
- weapon impact cue / weapon combat data

Attack entries do **not** own individual attack ranges.

### Variable AttackSet

`DA_RogueBossBasicAttacks > Attacks` contains a variable-length list of attack variations.

Each entry mainly defines:

- Attack ID / Type
- Attack Montage and Play Rate
- Selection Weight
- optional timed HitScan window
- optional individual Cooldown

`UGA_BossBasicAttack`:

1. builds the currently usable attack candidates;
2. excludes entries blocked by individual cooldowns;
3. can exclude the immediately previous attack when `Avoid Immediate Repeat` is enabled;
4. performs weighted selection;
5. plays the selected Montage;
6. applies the shared `Cooldown.Enemy.BasicAttack` plus any entry-specific cooldown.

Multiple Combo entries may share one cooldown tag to place the whole combo group on cooldown together.

---

## 3. BasicAttack Hit Detection

Two authoring paths exist; normally use only one per attack.

### Recommended: `ANS_HitScanWindow`

Place HitScan windows directly around the weapon-contact portions of the Montage.

- One window: target can be damaged once during that window.
- Multiple windows: suitable for Combo attacks.
- Starting a new window resets the already-hit target set, allowing the next swing to damage the same player again.

### Fallback: Timed HitScan Window

An AttackSet entry may instead define a normalized timed HitScan interval.

Do not combine the timed window with Montage HitScan windows for the same intended strike, because overlapping detection windows can duplicate attack evaluation.

---

## 4. BasicAttack Behavior Tree Flow

```text
Attack-range start condition
-> BTD_CanActivateAbilityByTag(GameplayAbility.BasicAttack)
-> BTT_ActivateBossAbility(GameplayAbility.BasicAttack)
```

Important settings/concepts:

- `Prefer Current Weapon Ability`: enabled
- `Require Preselected Destination`: disabled
- attack range comes from the equipped Sword
- the range condition determines whether the attack may **start**

Once the Montage has committed, the player leaving attack range does not cancel the BasicAttack. GAS-owned hit/death cancellation still applies.

---

## 5. DashSlash Runtime Architecture

`UGA_BossDashSlash` runs as a **ServerOnly** ability.

Gameplay progression is controlled by server-side section timing and timers, not AnimNotify gameplay events.

```text
Ability:
WindupEntering
-> WindupHolding
-> DashAttacking
-> WaitingForCompletion
-> Recovering
-> End

Montage:
Windup
-> WindupHold (loop)
-> DashSlash
-> DashHold (loop)
-> Recover
```

Required Montage sections:

- `Windup`
- `WindupHold`
- `DashSlash`
- `DashHold`
- `Recover`

The Montage is an **in-place FullBody** animation. Root Motion does not own the Dash movement.

---

## 6. DashSlash Timing & Completion Contract

`WindupHoldDuration` adds a server-controlled preparation hold after the normal Windup section.

When the Hold finishes, the server starts the `DashSlash` Montage section and actual Dash movement/damage evaluation together.

Movement completion and slash animation completion are independent:

```text
Arrive first
-> stop movement/damage
-> let DashSlash animation finish

Slash finishes first
-> stay in DashHold
-> wait for destination arrival

Both complete
-> Recover
-> Ability ends
```

This prevents network or frame timing differences from making animation completion incorrectly determine physical Dash completion.

Dash progress uses authoritative elapsed server time, while destination arrival remains the movement completion criterion.

---

## 7. Dash Destination & Moving-Ship Contract

The BT must prepare the destination before activating DashSlash.

```text
BTD_CanActivateAbilityByTag(GameplayAbility.Boss.DashSlash)
-> optional BTT_BossStrafe
-> BTT_SelectBossDestinationPoint(Purpose = Dash)
-> BTT_ActivateBossAbility(GameplayAbility.Boss.DashSlash)
```

Core destination rules:

- `DestinationPointId` is prepared before activation.
- invalid/default ID is `-1`.
- Dash requires sufficient minimum travel distance.
- `Require Dash Path Through Target` is enabled.
- Dash selection does not use the target-facing Front/Behind filter described for other mobility policies.

At Commit, DashSlash captures its start/end path in **HostShip-local space**.

During Windup:

- the Boss remains based on the moving Deck;
- its own AI movement / walking speed is locked;
- ship movement and tilt still carry the Boss correctly.

When Dash begins:

- movement state is restored;
- movement, Sphere Sweep, Telegraph, and execution-path presentation use the same committed local-space path;
- the Dash finishes at the committed endpoint.

This separates the Boss action from world-space ship motion.

---

## 8. Dash Collision & Damage

`DashDamageVolume` is provided natively by the Boss.

During Dash:

- Boss Capsule temporarily ignores `Pawn`;
- `DashDamageVolume` becomes `QueryOnly` and overlaps Pawns;
- the server additionally performs Sphere Sweeps between movement steps to prevent high-speed tunneling;
- each eligible Actor can receive damage only once per Dash.

On arrival, cancellation, hit/death interruption, or cleanup:

- Dash damage detection is disabled;
- the original Pawn collision response is restored.

---

## 9. DashSlash Presentation & Damage Feedback

### Path presentation

`GameplayCue.Path.Boss.DashSlash.Telegraph`
- active during Windup through an Infinite GameplayEffect;
- shows the committed upcoming path.

`GameplayCue.Path.Boss.DashSlash.Execution`
- uses a Duration GameplayEffect;
- keeps the executed path visible briefly after the Dash.

Both presentations use the same path data as the authoritative Dash.

### Damage feedback

`GameplayCue.Boss.Attack`
- optional attack-start presentation;
- not used as confirmed-damage feedback.

`GameplayCue.Boss.Hit`
- used when a player actually damages the Boss;
- executes only after Boss Health decreases;
- strong feedback is targeted to the instigating local player.

Weapon/Boss Impact Cue
- used when the Boss actually damages a player;
- the Impact Cue tag is carried with the outgoing Damage Spec;
- the victim ASC executes the cue after confirmed Health loss;
- target-local camera/VFX/audio therefore correspond to the actually damaged player.

No Health decrease means no confirmed-hit feedback cue.

---

## 10. Minimal Integration Checklist

### BasicAttack
- Boss has `UGA_BossBasicAttack`.
- `Basic Attack Set` points to `DA_RogueBossBasicAttacks`.
- Sword provides shared range/damage data.
- Combat BT contains the BasicAttack branch.
- Each AttackSet entry has a valid Montage and HitScan authoring method.

### DashSlash
- Boss has the Blueprint/native DashSlash ability only once.
- Dash Montage contains all five required sections.
- BT selects `DestinationPointId` before Dash activation.
- minimum-distance and path-through-target policies agree with the Dash ability.
- Dash uses the native damage volume and server Sweep logic.

### Multiplayer
Verify that:

- BasicAttack and Dash Montage phases appear consistently on clients;
- Dash converges to the server-authoritative destination;
- one Dash does not damage the same Actor repeatedly;
- AnimNotify does not drive duplicate Dash/damage gameplay;
- Impact/Hit feedback appears only after authoritative Health loss.
