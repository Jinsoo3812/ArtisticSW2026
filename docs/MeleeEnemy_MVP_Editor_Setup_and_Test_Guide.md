# MeleeEnemy 최소 MVP 에디터 설정 및 테스트 가이드

## 1. 구현된 런타임 규칙

- `AMeleeEnemy`는 지상형 근거리 적이며, 컨트롤러 Focus 회전을 사용한다.
- 무기는 스폰 시 등에 장착되고 전투 진입 BT Task에서 손으로 장착된다.
- 실제 공격 가능 거리는 `WeaponCombatData.AttackRange` 하나만 사용한다.
- 기본 공격 쿨다운은 GAS의 `Cooldown.Enemy.BasicAttack` 태그와 Duration Gameplay Effect로 관리한다.
- 달리기/Strafe 속도는 기존 `BTT_SetMovementSpeed`가 담당한다.
- Strafe 반지름과 포인트 수는 EQS 자산이 담당하고, 대기 시간은 BT `Wait` 노드가 담당한다.
- 두 플레이어가 동시에 보이더라도 현재 유효한 전투 대상을 유지한다. 현재 대상이 죽거나 시야에서 완전히 이탈한 뒤에만 다른 플레이어를 선택한다.

`AttackRange`와 EQS 반지름은 의미가 다르다. 첫 시작값은 아래처럼 둔다.

| 값 | 권장 시작값 | 의미 |
|---|---:|---|
| Weapon `AttackRange` | 180 cm | 공격 Task가 도달해야 하는 실제 무기 사거리 |
| EQS Circle Radius | 350 cm | 공격 대기 중 Target 주위를 도는 전술 반지름 |
| Reapproach Distance | 500 cm | Target이 멀어졌을 때 Strafe를 중단하는 거리 |
| Basic Attack Cooldown | 2.0 s | GAS 쿨다운 태그 유지 시간 |

## 2. Editor를 다시 연 뒤 Blueprint 설정

1. `BP_MyEnemy`를 연다.
2. `File > Reparent Blueprint`에서 부모를 `MeleeEnemy`로 변경한다.
3. Class Defaults에서 다음 상속값을 확인한다. Blueprint에 노란색 Override 화살표가 있으면 부모 기본값으로 Reset한다.
   - `Use Controller Rotation Yaw = true`
   - Character Movement의 `Orient Rotation to Movement = false`
   - Character Movement의 `Use Controller Desired Rotation = false`
   - `Equip Weapon On Spawn = false`
   - AI Controller Class = `BaseAIController`
   - Auto Possess AI = `Placed in World or Spawned`
4. 기존 Skeletal Mesh, Anim Class, ASC/Attribute 설정, `DefaultWeaponTag`, Weapon Registry와 장착 Socket 설정이 유지됐는지 확인한다.
5. Compile 후 Save한다.

`Set Focus`가 동작하지 않던 핵심 조건은 Pawn 회전 설정이다. 위 세 회전 값이 부모와 다르게 Override되어 있으면 BT의 Focus가 설정돼도 CharacterMovement 회전이 이를 덮어쓸 수 있다.

## 3. 무기와 공격 쿨다운 설정

### `DA_Weapon`

1. `/Game/GameplayAbilitySystem/Enemy/Weapon/DA_Weapon`을 연다.
2. Combat Data의 `Attack Range`를 우선 `180`으로 설정한다.
3. 기존 `Max Attack Range` 값은 Core Redirect로 `Attack Range`에 이전된다. 자산을 열어 값이 정상인지 확인하고 Save해 직렬화 형식을 갱신한다.
4. `Min Attack Range`, `Max Attack Range`, `Attack Cooldown`은 더 이상 Weapon Data에 두지 않는다.

### `BPGA_MeleeAttack`

1. `/Game/GameplayAbilitySystem/Enemy/GAS/Ability/BPGA_MeleeAttack`을 연다.
2. 부모가 `GA_BasicAttack` 계열인지 확인한다.
3. `Attack > Cooldown > Attack Cooldown Duration`을 우선 `2.0`초로 둔다.
4. 기존 Blueprint Cooldown GE나 BT Cooldown Decorator가 별도로 있다면 제거해 중복 쿨다운을 피한다.

## 4. Melee Strafe EQS 만들기

권장 자산 이름:

```text
/Game/GameplayAbilitySystem/Enemy/AI/EQS/EQS_Melee_StrafePosition
```

1. `Points: Circle` Generator를 추가한다.
2. Circle Center를 기존 C++ Context인 `Enemy Combat Target`으로 지정한다.
3. Radius `350`, Number of Points `8`로 시작한다.
4. Navigation Projection을 켜고 NavMesh로 투영한다.
5. `Pathfinding` Test를 추가해 Querier에서 후보까지 `Path Exist = true`만 통과시킨다.
6. 필요하면 `Distance to Querier`를 `100~500`, `Filter and Score`로 추가해 너무 짧거나 과도한 이동을 제한한다.
7. BT의 `Run EQS Query`는 `Random Best 25%`, 결과 Blackboard Key는 기존 `PointOfInterest`로 설정한다.

포인트 수와 Circle 반지름은 EQS에서만 조절한다. Strafe 사이의 체류 시간은 EQS가 아니라 BT `Wait`에서 조절한다.

## 5. `BT_Subtree_MeleeEnemy_Combat` 구성

Root 아래에 TargetActor가 Set인 동안만 실행되는 Combat Sequence를 만들고, 아래 순서를 사용한다.

```text
Root
└─ Sequence: Melee Combat                     [TargetActor Is Set, Observer Aborts Both]
   ├─ Equip Enemy Weapon
   ├─ Sequence: Initial Approach
   │  ├─ Clear Focus
   │  ├─ Set Movement Speed: Run
   │  └─ Move To: TargetActor                 [Acceptable Radius 350, Track Moving Goal]
   ├─ Sequence: Initial Strafe
   │  ├─ Set Focus: TargetActor
   │  ├─ Run EQS Query -> PointOfInterest      [Random Best 25%]
   │  ├─ Set Movement Speed: Strafe
   │  ├─ Move To: PointOfInterest              [Allow Strafe, Radius 15~50]
   │  └─ Wait                                  [0.2 s, Random Deviation 0.1]
   └─ Selector: Combat Loop                    [Loop: Infinite]
      ├─ Sequence: Reapproach                  [Target Distance: Outside 500]
      │  ├─ Clear Focus
      │  ├─ Set Movement Speed: Run
      │  └─ Move To: TargetActor               [Acceptable Radius 350, Track Moving Goal]
      ├─ Sequence: Attack                      [Is Melee Attack Ready]
      │  ├─ Clear Focus
      │  ├─ Set Movement Speed: Run
      │  ├─ Move To Weapon Range               [TargetActor]
      │  ├─ Set Focus: TargetActor
      │  ├─ Set Movement Speed: Idle
      │  └─ Enemy Basic Attack
      └─ Sequence: Strafe
         ├─ Set Focus: TargetActor
         ├─ Run EQS Query -> PointOfInterest   [Random Best 25%]
         ├─ Set Movement Speed: Strafe
         ├─ Move To: PointOfInterest           [Allow Strafe, Radius 15~50]
         └─ Wait                               [0.2 s, Random Deviation 0.1]
```

중요한 설정:

- `Target Distance` 기본값은 `Outside`, `500`, 2D, Observer Aborts `Both`다. 0.1초 간격으로 거리를 다시 평가하므로 TargetActor 포인터가 바뀌지 않아도 Strafe를 중단한다.
- `Is Melee Attack Ready`는 Observer Aborts `Lower Priority`다. 쿨다운 태그가 사라지는 순간 하위 Strafe 분기를 중단하고 Attack 분기로 진입한다.
- `Move To Weapon Range`는 매 실행 시 현재 무기의 `AttackRange`를 읽는다. 이 Task의 Acceptable Radius를 에디터에서 별도로 맞출 필요가 없다.
- Attack 분기에 BT `Cooldown` Decorator를 추가하지 않는다.
- Combat Loop에 Infinite Loop Decorator를 붙여 Initial Approach/Initial Strafe는 전투 진입 시 한 번만 실행되게 한다.

## 6. 단계별 확인 사항

### 단계 A: 장착 및 접근

- 전투 전 무기가 등 Socket에 있는가?
- Combat 진입 시 `Equip Enemy Weapon` 후 손 Socket으로 이동하는가?
- 350 cm 밖에서는 Run 속도로 빠르게 접근하는가?

### 단계 B: Strafe와 Focus

- 첫 공격 전에 Initial Strafe가 최소 한 번 실행되는가?
- 이동 중 몸 방향이 `TargetActor`를 향하는가?
- EQS 후보가 NavMesh 위에 있고 `PointOfInterest`가 갱신되는가?
- Target과의 거리가 500 cm를 넘으면 현재 Strafe가 중단되고 Reapproach가 실행되는가?

### 단계 C: 공격과 쿨다운

- 공격 분기에서 실제 무기 `AttackRange`까지 접근하는가?
- 공격 중 `State.Attacking`이 유지되고 종료 시 BT Task가 끝나는가?
- 공격 직후 `Cooldown.Enemy.BasicAttack`이 약 2초간 생기는가?
- 쿨다운 동안 Strafe하고, 태그가 사라지면 Attack이 Strafe를 선점하는가?
- Montage Notify 기반 판정과 데미지가 한 번만 발생하는가?

## 7. 2인 멀티플레이 PIE 테스트

PIE를 `Number of Players = 2`, 우선 `Play As Listen Server`로 실행하고 이후 Dedicated Server 모드도 확인한다.

1. 두 플레이어를 모두 Sight 범위에 넣는다.
2. 먼저 선택된 Target이 살아 있고 유효한 동안 두 번째 플레이어 쪽으로 Target이 계속 바뀌지 않는지 확인한다.
3. 현재 Target만 공격받고 데미지가 서버에서 한 번만 적용되는지 확인한다.
4. 현재 Target을 죽이거나 Lose Sight 시간 이상 완전히 숨긴다.
5. 남은 유효 Player로 Target이 전환되는지 확인한다.
6. 두 Client에서 무기 장착, 회전, Montage와 위치가 동일하게 보이는지 확인한다.
7. 공격 쿨다운과 AI/BT 판단이 서버에서만 권위 있게 실행되는지 확인한다.

Gameplay Debugger와 `showdebug abilitysystem`을 사용하면 Blackboard Target, BT 활성 분기, `State.Attacking`, `Cooldown.Enemy.BasicAttack`을 확인하기 쉽다.

## 8. MVP 완료 기준

- 전투 진입: 등 장착 -> 손 장착 -> 350 cm 접근 -> Initial Strafe 순서가 보인다.
- 전투 루프: 쿨다운 준비 시 실제 무기 사거리까지 접근해 공격하고, 쿨다운 동안 EQS Strafe한다.
- 이탈 대응: Target이 500 cm 밖으로 벗어나면 Strafe보다 재접근이 우선한다.
- 회전: 이동/대기/공격 직전 모두 Focus가 TargetActor를 향하게 만든다.
- 멀티플레이: 서버 권위로 한 Target을 안정적으로 유지하며, 유효성을 잃었을 때만 다른 Player로 전환한다.
