# Ground Enemy Spawner, Passive Smart Object, and Alarm Setup

## 1. Enemy type tags and catalog

The project defines exact `Enemy.Type` tags for every row family in
`DataTable/EnemyBaseStats.csv`:

```text
Enemy.Type.Ground.Melee.T1 ... T4
Enemy.Type.Ground.Ranged.T1 ... T4
Enemy.Type.Deck.Melee.T1 ... T4
Enemy.Type.Deck.Ranged.T1 ... T4
Enemy.Type.BossSummon.Ranged.T1 ... T4
Enemy.Type.Boss.T1 ... T4
```

Create a `UEnemySpawnCatalog` Data Asset and add one entry for each enemy that
can be spawned. Each entry owns the exact mapping:

```text
EnemyTypeTag -> Enemy Blueprint class -> DT_EnemyBaseStats row
```

Set the same `EnemyTypeTag` on the Enemy Blueprint class default. The spawner
rejects a class whose existing tag disagrees with the catalog, which prevents a
spawned tier/class from silently using another enemy's drop identity.

## 2. Ground spawner

Create a Blueprint derived from `AGroundEnemySpawner`, assign the catalog, and
fill `SpawnEntries` with a tag and count. The spawner resolves the class and
stats row from the catalog, chooses reachable NavMesh locations, and assigns the
territory before the enemy controller starts.

The radii must satisfy:

```text
SpawnRadius <= PatrolRadius <= CombatRadius
```

The assigned territory initializes the shared Blackboard `HomeLocation` and
`PatrolRadius`. A combat target outside `CombatRadius` is rejected; a current
target that leaves the radius is cleared on the controller's territory check.

## 3. Passive behavior without EQS

`BTT_FindAndUsePatrolSmartObject` queries the Smart Object subsystem directly.
It does not execute EQS. It searches around `HomeLocation`, limits results to
`PatrolRadius`, claims one available slot, moves to it, and runs its Gameplay
Behavior.

The available passive activity tags are:

```text
AI.Activity.Enemy.Passive.Watch
AI.Activity.Enemy.Passive.Rest
AI.Activity.Enemy.Passive.Inspect
```

Add these activity tags to Smart Object slot definitions and configure a
Gameplay Behavior for each slot. In the Passive subtree, place the Smart Object
branch above or beside the ordinary NavMesh patrol branch. Let task failure fall
through to `Find Patrol Location -> Move To -> Wait`, so enemies still patrol
when every Smart Object is occupied.

State decorators on the Passive branch must abort the task when state changes.
The task then cancels movement/behavior and releases the claimed slot.

## 4. Player sight alarm

Place `BTT_BroadcastEnemyAlarm` after `TargetActor` becomes valid, normally near
the beginning of the Combat subtree. The task reports a Hearing stimulus at the
observing enemy's own location. It never publishes the player's location or
assigns the player as another AI's combat target.

Nearby `ABaseAIController` listeners receive the stimulus through their existing
Hearing sense:

```text
Alarm source location -> PointOfInterest -> Investigating state
```

After the investigation subtree reaches `PointOfInterest`, its existing policy
returns the enemy to Passive unless that enemy sees or is damaged by the player.
The task includes a per-AI cooldown so a looping Combat subtree cannot broadcast
an alarm every frame. `MaxRange` also remains limited by each listener's authored
Hearing range.
