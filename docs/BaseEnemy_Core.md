# Enemy Core Architecture Guide

이 문서는 기본 Enemy AI의 공통 구조를 빠르게 파악하기 위한 요약본이다.  
세부 에디터 클릭 순서, 디버깅 절차, 테스트 명령어, Boss 전용 기능은 제외하고 핵심 책임과 실행 흐름만 정리한다.

---

## 1. 전체 구조

```text
ABaseEnemy
├─ ASC / Attribute / Health / Death
├─ Weapon
└─ Movement

ABaseAIController
├─ Sight / Hearing / Damage Perception
├─ TargetActor 관리
└─ EEnemyAIState 관리
        |
        v
BT_EnemyBase
└─ State에 따라 Dynamic Subtree 선택
        |
        v
UEnemyBehaviorSet
└─ Enemy 종류별 Passive / Combat Subtree 교체
```

핵심 원칙은 다음과 같다.

- **Controller**는 감지와 고수준 상태를 관리한다.
- **Root Behavior Tree**는 현재 State에 맞는 행동 트리를 선택한다.
- **Subtree**는 실제 순찰, 조사, 이동, 공격을 수행한다.
- **Gameplay Ability System**은 공격, 버프, Cooldown 같은 원자적 Gameplay Action을 담당한다.
- Enemy 종류별 차이는 가능한 한 `BehaviorSet + Subtree`로 분리한다.

---

## 2. High-Level AI State

공통 상태는 Native Enum `EEnemyAIState`를 사용한다.

```text
Passive
Investigating
Combat
Frozen
Dead
```

의미:

- `Passive`: Idle / Guard / Patrol
- `Investigating`: 소리나 관심 위치 조사
- `Combat`: 타깃 추적, 위치 조정, 공격
- `Frozen`: 외부 상태 이상으로 행동 중지
- `Dead`: 사망 최종 상태

`Combat`과 GAS의 `State.Attacking`은 다른 개념이다.

```text
Combat
= 장시간 유지되는 AI 의도

State.Attacking
= 한 번의 공격 Ability가 실행 중인 짧은 상태
```

Blackboard의 `State`는 Blueprint Enum이 아니라 `EEnemyAIState`를 그대로 사용한다.

---

## 3. Perception과 Target 관리

`ABaseAIController`가 Sight, Hearing, Damage를 공통 처리한다.

```text
Sight
-> 유효 TargetActor 설정
-> State = Combat

Hearing
-> PointOfInterest = Noise Location
-> State = Investigating

Damage
-> 실제 공격자를 TargetActor로 설정
-> State = Combat
```

### Target 유지 정책

현재 Combat Target이 아직 유효하다면 불필요하게 다른 플레이어로 계속 교체하지 않는다.

Sight를 잃었을 때:

```text
현재 Target 상실
-> 다른 유효 감지 Target 검색
-> 있으면 교체
-> 없으면 TargetActor Clear
-> Passive
```

### Damage Sense

Damage Perception은 **실제 Health 감소가 발생했을 때만** 보고한다.

즉,

```text
피격 연출만 발생
회복
Instigator 없는 환경 Damage
```

같은 상황은 임의의 Combat Target을 만들지 않는다.

### Hearing

단순히 Sound를 재생한다고 AI가 듣는 것은 아니다.

Gameplay 쪽에서 다음과 같은 실제 Noise Event를 보고해야 한다.

```text
UAISense_Hearing::ReportNoiseEvent
또는
Pawn::MakeNoise
```

---

## 4. Blackboard 계약

공통 Blackboard는 `BB_EnemyBase`를 사용한다.

주요 Key:

```text
State
TargetActor
PointOfInterest
HomeLocation
PatrolLocation
PatrolRadius
```

`PointOfInterest`는 상태에 따라 의미를 공유할 수 있다.

```text
Investigating -> 소리 / 관심 위치
Combat       -> 전투용 이동 위치
```

동시에 여러 State Subtree가 실행되지 않기 때문에 같은 Vector Key를 재사용할 수 있다.

---

## 5. Root Behavior Tree

`BT_EnemyBase`는 실제 전투 로직을 직접 가지지 않고 **State Router** 역할만 한다.

```text
Root Selector
├─ Dead
├─ Frozen
├─ Combat
├─ Investigating
└─ Passive
```

각 Branch:

```text
State == X
-> Run Behavior Dynamic
```

State Decorator는 값이 바뀌면 현재 Subtree를 Abort하고 새로운 State Branch를 선택한다.

### 공통 Subtree

```text
Passive
-> Patrol / Guard / Wait

Investigating
-> Move To PointOfInterest
-> Wait
-> Passive

Frozen
-> Stop Movement
-> Wait

Dead
-> Stop Movement
-> Wait
```

Death 처리 자체는 Death GA / `ABaseEnemy`가 담당하고, Dead BT는 추가 이동과 공격만 막는다.

---

## 6. BehaviorSet과 Enemy 확장

`UEnemyBehaviorSet`은 다음을 매핑한다.

```text
Enemy State
+ Injection Tag
+ State 전용 Subtree
```

예:

```text
DA_EnemyBehavior_Melee
├─ Passive -> Melee Passive Subtree
└─ Combat  -> Melee Combat Subtree
```

공통 `Investigating`, `Frozen`, `Dead`는 Root BT의 Default Subtree를 그대로 사용할 수 있다.

따라서 Enemy 종류를 확장할 때 Root BT를 복제하기보다:

```text
공통 Root BT
+ Enemy별 BehaviorSet
+ 필요한 State Subtree만 교체
```

하는 구조를 사용한다.

---

## 7. Movement와 Weapon Range

이동 속도와 무기 사거리는 각각 하나의 기준점을 가진다.

### Movement Speed

기본 이동 속도는 `BTT_SetMovementSpeed` 같은 locomotion task가 결정한다.

```text
Idle
Jog
Strafe
Run
```

MoveSpeed Buff가 있으면 최종 속도는:

```text
(BaseMovementSpeed * SpawnMovementSpeedMultiplier)
+ MoveSpeedBonus
```

으로 계산한다.

중요한 규칙:

```text
BaseMovementSpeed == 0
-> 최종 이동 속도도 0
```

따라서 Buff가 Idle/Frozen 상태를 우회하지 못한다.

### Weapon Range

실제 공격 가능 거리는 현재 장착 무기의:

```text
Weapon CombatData.AttackRange
```

를 기준으로 한다.

BT에 같은 거리 값을 다시 하드코딩하지 않는다.

`BTT_MoveToWeaponRange`는 현재 무기의 AttackRange를 읽고 Target을 추적해 실제 공격 가능한 거리까지 이동한다.

Task 성공 또는 Abort 시 남은 이동을 즉시 정리한다.

---

## 8. Ability Activation 공통 구조

Behavior Tree에서는 Ability Class를 직접 하드코딩하기보다 Gameplay Tag를 사용한다.

```text
Can Activate Ability By Tag
-> Activate Enemy Ability By Tag
```

예:

```text
GameplayAbility.Enemy.Buff.MoveSpeed
```

`BTT_ActivateEnemyAbilityByTag`는 서버에서 해당 태그의 정확한 Ability Spec을 실행하고 필요한 경우 종료까지 기다린다.

MoveSpeed Boost는:

```text
Ability 실행
-> Duration GameplayEffect 적용
-> Ability 자체는 즉시 종료
```

구조이므로 BT Branch가 이후 Abort되어도 이미 적용된 Buff Effect는 Duration 동안 유지된다.

---

## 9. Melee Enemy 적용 예시

Melee Enemy도 동일한 State/Perception/Root BT 구조를 사용하고 Combat Subtree만 특화한다.

대표 흐름:

```text
Combat 진입
-> Weapon Equip
-> Initial Approach
-> Initial Strafe
-> Combat Loop
```

Combat Loop:

```text
Target이 너무 멀다
-> Reapproach

공격 준비 완료
-> Move To Weapon Range
-> Focus Target
-> Idle
-> Basic Attack

공격 Cooldown 중
-> EQS Strafe
```

핵심 원칙:

- 실제 공격 거리 = Weapon `AttackRange`
- 공격 Cooldown = GAS `Cooldown.Enemy.BasicAttack`
- BT에 별도의 동일 Cooldown 값을 복사하지 않는다.
- 공격 가능 상태가 되면 Attack Branch가 Strafe보다 높은 우선순위를 가진다.
- 이동/Strafe 중에는 `Set Focus(TargetActor)`로 적을 바라보게 한다.
- Target이 멀어지면 Strafe보다 Reapproach가 우선한다.

즉 Melee Combat은 다음 패턴으로 이해하면 된다.

```text
Approach
-> Positioning
-> Attack
-> Cooldown 동안 Positioning
-> 다시 Attack
```

---

## 10. 공통 설계 원칙 정리

전체 Enemy 시스템의 핵심 규칙은 다음과 같다.

```text
Perception
-> State 결정

State
-> Root BT가 Subtree 선택

Subtree
-> 이동 / 위치 선정 / 공격 결정

Weapon Data
-> 실제 공격 거리

GAS
-> 공격 / Cooldown / Buff

CharacterMovement
-> 실제 이동

Server
-> AI 판단과 Ability 실행의 권위
```

새 Enemy Archetype을 만들 때는 먼저 공통 구조를 재사용하고, 필요한 행동 차이만 BehaviorSet과 Subtree로 교체하는 것이 기본 방향이다.
