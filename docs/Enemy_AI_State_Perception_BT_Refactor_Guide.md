# Enemy State / Perception / Behavior Tree 리팩터링 및 에디터 설정 가이드

## 1. 문서 목적

이 문서는 다음 C++ 리팩터링을 Unreal Editor 자산에 연결하는 절차를 설명한다.

- `EEnemyAIState` Native Enum으로 Enemy의 고수준 상태 관리
- 공통 `ABaseAIController`에서 Sight, Hearing, Damage 감지
- 실제 GAS 체력 감소를 AI Damage Sense에 보고
- 공통 Root Behavior Tree가 `State`로 상태를 라우팅
- `UEnemyBehaviorSet` Data Asset으로 Enemy별 Dynamic Subtree 주입
- 지상 `ARangedEnemy`가 BT에서 이동, 재배치, 원거리 공격 수행
- 이후 `ADeckRangedEnemy : ARangedEnemy`가 갑판 전용 Subtree만 교체할 수 있는 기반 마련

Player 테스트 키, 테스트용 `Make Noise`, 테스트용 Damage 입력은 구현하지 않는다.

---

## 2. 구현된 C++ 구조

### 2.1 상태 타입

파일:

```text
Source/Enemy/Public/AI/EnemyAITypes.h
```

상태:

| State | 저장 값 | 의미 |
|---|---:|---|
| `Passive` | 0 | 대기, 경계, 순찰 |
| `Investigating` | 10 | 소리 위치 조사 |
| `Combat` | 20 | 표적 추적, 재배치, 공격 |
| `Frozen` | 30 | 행동 중지 또는 외부 상태 이상 |
| `Dead` | 250 | 사망 최종 상태 |

Blackboard Enum은 숫자로 저장되므로 기존 값은 변경하거나 재사용하지 않는다. 새 상태를 추가할 때는 사용하지 않은 값만 배정한다.

`Combat`과 GAS의 `State.Attacking`은 다른 개념이다.

- `Combat`: 장시간 유지되는 AI 의도
- `State.Attacking`: 한 번의 공격 Ability가 실행 중인 짧은 상태

### 2.2 Behavior Set

파일:

```text
Source/Enemy/Public/AI/EnemyBehaviorSet.h
Source/Enemy/Private/AI/EnemyBehaviorSet.cpp
```

`UEnemyBehaviorSet`은 다음 매핑을 가진다.

```text
Enemy State
+ Run Behavior Dynamic Injection Tag
+ State 전용 Behavior Tree Subtree
```

Root BT의 Default Behavior Asset은 공통 fallback으로 사용하고, Behavior Set에는 해당 Enemy가 교체할 상태만 넣을 수 있다.

### 2.3 Perception과 상태 전환

`ABaseAIController`가 다음 감각을 한 이벤트에서 구분한다.

```text
OnTargetPerceptionUpdated
  Sight   -> 유효 표적이면 TargetActor 설정, State = Combat
  Hearing -> PointOfInterest 설정, State = Investigating
  Damage  -> 유효 공격자를 TargetActor로 설정, State = Combat
```

기본 전환 정책:

| 현재 상태 | Sight | Hearing | Damage |
|---|---|---|---|
| Passive | Combat | Investigating | Combat |
| Investigating | Combat | 위치 갱신 | Combat |
| Combat | 표적 유지/교체 | 무시 | 유효 공격자로 교체 |
| Frozen | 무시 | 무시 | 무시 |
| Dead | 무시 | 무시 | 무시 |

Sight를 잃었을 때 현재 감지 중인 다른 유효 표적을 먼저 찾는다. 대체 표적이 없을 때만 `TargetActor`를 지우고 `Passive`로 돌아간다.

### 2.4 실제 피해와 Damage Sense

`ABaseEnemy::OnHealthChanged`가 서버에서 실제 체력이 감소했을 때만 `UAISense_Damage::ReportDamageEvent`를 호출한다.

조건:

```text
HasAuthority
OldHealth > NewHealth
InstigatorActor 유효
InstigatorActor != Damaged Enemy
```

따라서 단순한 피격 연출이나 회복은 Damage 자극을 만들지 않는다. GAS Effect Context의 `OriginalInstigator`, `EffectCauser`, `SourceObject`에서 복원된 공격자가 자극의 원인이 된다.

### 2.5 RangedEnemy 공격 경계

`ARangedEnemyAIController`의 영구 10Hz 공격 Timer는 제거했다. Controller는 Perception과 상태 갱신만 담당한다.

다음 코드는 계속 C++/GAS에 남는다.

- 표적 유효성
- Min/Max Range
- LOS Trace
- 공격 Cooldown
- `State.Attacking` 중복 방지
- Ability 실행
- Montage Event
- Projectile 생성과 최종 표적 재검증

BT용 노드:

| 노드 | 역할 |
|---|---|
| `Can Ranged Attack` Decorator | RangedEnemy의 Target/Range/LOS 규칙 확인 |
| `Ranged Attack` Task | Blackboard Target을 공격 대상으로 설정하고 GAS 공격 완료까지 대기 |
| `Set Enemy State` Task | Subtree 종료 시 공통 방식으로 State 변경 |

`Ranged Attack` Task는 `ARangedEnemy`의 남은 Cooldown을 직접 읽어 기다린다. BT에 같은 Cooldown 수치를 다시 입력하지 않는다.

---

## 3. C++ 반영 후 최초 에디터 준비

1. Unreal Editor를 종료한다.
2. `ArtisticSW2026Editor Win64 Development`를 빌드한다.
3. Editor를 다시 실행한다.
4. `Tools > Refresh Visual Studio Project`는 IDE 파일이 필요할 때만 실행한다.
5. `Project Settings > Gameplay Tags`에서 다음 태그가 검색되는지 확인한다.

```text
AI.Behavior.Passive
AI.Behavior.Investigating
AI.Behavior.Combat
AI.Behavior.Frozen
AI.Behavior.Dead
```

검색되지 않으면 Editor를 완전히 종료한 뒤 다시 실행한다. 태그는 `Config/Tags/Enemy.ini`에서 Import된다.

권장 자산 폴더:

```text
Content/GameplayAbilitySystem/Enemy/AI/
  Root/
  Subtrees/Common/
  Subtrees/RangedGround/
  BehaviorSets/
```

기존 경로를 유지해야 한다면 `BB_EnemyBase`, `BT_EnemyBase`는 이동하지 않고 그 자리에서 수정해도 된다.

---

## 4. BB_EnemyBase 수정

대상:

```text
Content/GameplayAbilitySystem/Enemy/AI/BB_EnemyBase
```

기존 키를 유지한다.

```text
TargetActor
HomeLocation
PatrolLocation
PatrolRadius
```

다음 키를 추가한다.

### 4.1 State

1. Blackboard Editor에서 `New Key`를 누른다.
2. Key Type을 `Enum`으로 선택한다.
3. 이름을 정확히 `State`로 지정한다.
4. 생성한 `State` Key를 선택하고 Details에서 Enum Key Type 설정을 펼친다.
5. `Enum Type` 선택기에서 찾는 대신 `Enum Name`에 `EEnemyAIState`를 직접 입력하고 Enter를 누르거나 포커스를 다른 곳으로 옮긴다.
6. `Is Enum Name Valid`가 활성화되는지 확인한다.
7. 짧은 이름이 인식되지 않으면 `Enum Name`에 전체 Reflection 경로인 `/Script/Enemy.EEnemyAIState`를 입력한다.
8. Instance Synced는 끈다.

`Enum Type` 선택기는 Content Browser의 User Defined Enum 위주로 표시되기 때문에 C++ Native Enum인 `EEnemyAIState`가 검색 결과에 보이지 않을 수 있다. 이를 대신하는 별도의 Blueprint Enum을 만들지 않는다. 두 Enum을 병행하면 C++ State API와 BT Decorator가 서로 다른 타입을 참조하게 된다.

`Is Enum Name Valid`가 켜지지 않으면 Editor를 종료하고 Live Coding이 아닌 전체 Editor Target 빌드를 한 뒤 다시 실행한다. 새 `UENUM`/`UCLASS` Reflection 타입은 Editor가 열린 상태의 Live Coding만으로 등록되지 않을 수 있다.

Controller는 Possess 시 `Passive`로 초기화한다.

### 4.2 PointOfInterest

1. Key Type을 `Vector`로 추가한다.
2. 이름을 정확히 `PointOfInterest`로 지정한다.
3. Instance Synced는 끈다.

Hearing 자극의 `StimulusLocation`이 이 키에 저장된다.

### 4.3 타입 확인

| Key | 타입 |
|---|---|
| TargetActor | Object, Base Class = Actor |
| HomeLocation | Vector |
| PatrolLocation | Vector |
| PatrolRadius | Float |
| State | Enum, `EEnemyAIState` |
| PointOfInterest | Vector |

이름의 대소문자와 공백을 변경하지 않는다. 다른 프로젝트 규칙이 필요하면 Controller Blueprint의 `AI > Blackboard` Key Name 속성도 함께 변경해야 한다.

---

## 5. 공통 Subtree 생성

모든 Subtree의 Blackboard Asset은 `BB_EnemyBase`로 지정한다. Subtree 안에는 State 분기 Decorator를 다시 만들지 않는다. State 분기는 Root BT 한 곳에서만 관리한다.

### 5.1 BT_Subtree_Common_Passive

기존 `BT_EnemyBase`의 순찰 로직을 이 자산으로 옮긴다.

예시:

```text
Root
  Sequence
    Find Patrol Location
    Move To PatrolLocation
    Wait
```

기존 `HomeLocation`, `PatrolLocation`, `PatrolRadius` 키를 그대로 사용한다.

### 5.2 BT_Subtree_Common_Investigating

```text
Root
  Sequence
    Move To PointOfInterest
    Wait 3.0
    Set Enemy State
```

`Move To` 설정:

- Blackboard Key: `PointOfInterest`
- Observe Blackboard Value: On
- Allow Partial Path: 필요 시 On
- Acceptable Radius: 50~100cm에서 맵 규모에 맞춰 조정

`Set Enemy State` 설정:

- New State: `Passive`
- Clear Target Actor: Off
- Clear Point Of Interest: On
- Point Of Interest Key Name: `PointOfInterest`

조사 중 Sight 또는 Damage로 State가 `Combat`으로 바뀌면 Root의 State Decorator가 이 Subtree를 Abort한다.

### 5.3 BT_Subtree_Common_Frozen

```text
Root
  Sequence
    Stop Movement
    Wait 60.0
```

Wait가 끝나도 State가 Frozen이면 Root가 다시 같은 Subtree를 선택한다. Frozen 해제는 상태 효과 또는 외부 시스템이 `SetEnemyState`를 호출해 처리한다.

### 5.4 BT_Subtree_Common_Dead

사망, Ragdoll, Drop은 기존 `ABaseEnemy`와 Death GA가 처리한다. BT에서는 추가 이동과 공격만 방지한다.

```text
Root
  Sequence
    Stop Movement
    Wait 60.0
```

Dead Subtree에 Drop, Destroy, Ragdoll을 중복 구현하지 않는다.

---

## 6. 지상 RangedEnemy Subtree 생성

### 6.1 BT_Subtree_RangedGround_Passive

첫 버전은 두 방식 중 하나를 선택한다.

경비형:

```text
Root
  Sequence
    Wait 1~2초
```

순찰형:

```text
Root
  Sequence
    Find Patrol Location
    Move To PatrolLocation
    Wait
```

각 이동 Sequence 시작에는 `Set Movement Speed` Task를 둔다. `Jog` 모드는 350cm/s를 사용한다.

### 6.2 BT_Subtree_RangedGround_Combat

권장 첫 구조:

```text
Root
  Selector
    Sequence: Attack
      Decorator: Can Ranged Attack
      Set Movement Speed: Idle (0)
      Set Focus: TargetActor
      Ranged Attack
      Clear Focus

    Sequence: Idle
      Set Movement Speed: Idle (0)
      Set Focus: TargetActor
      Wait
      Clear Focus
```

`Can Ranged Attack` 설정:

- Blackboard Key: `TargetActor`
- Require Line Of Sight: On
- Observer Aborts: None

`Ranged Attack` 설정:

- Blackboard Key: `TargetActor`
- Attack Execution State Tag: `State.Attacking`
- Cancel Ability On Abort: On

`Set Focus` 설정:

- Blackboard Key: `TargetActor`
- 기존 회전 정책을 유지

Sequence가 끝나면 `Clear Focus`로 Gameplay Focus를 해제한다.

`Set Movement Speed`의 모드별 속도:

| 모드 | Max Walk Speed |
|---|---:|
| Idle | 0 |
| Jog | 350 |
| Strafe | 250 |
| Run | 500 |

Reposition/Strafe는 기존 원형 이동 Task를 사용하지 않는다. 이후 EQS로 위치를 선정하는 Sequence를 추가하고, 시작에 `Set Movement Speed: Strafe (250)`, 종료에 `Clear Focus`를 둔다. 추적용 Run Sequence는 `Set Movement Speed: Run (500)`으로 시작한다.

`Ranged Attack` Task는 Cooldown 중에는 Task 내부에서 기다린다. 별도의 BT Cooldown Decorator에 `AttackCooldown` 값을 복사하지 않는다.

---

## 7. BT_EnemyBase를 Root State Router로 변경

대상:

```text
Content/GameplayAbilitySystem/Enemy/AI/BT_EnemyBase
```

Blackboard Asset은 `BB_EnemyBase`를 유지한다.

Root 아래에 Selector를 만들고 왼쪽에서 오른쪽으로 다음 순서를 사용한다.

```text
Dead
Frozen
Combat
Investigating
Passive
```

각 Branch 구조:

```text
Sequence
  Blackboard Decorator: State == 해당 State
  Run Behavior Dynamic
```

Blackboard Decorator 공통 설정:

- Key Query: Is Equal To
- Blackboard Key: `State`
- Key Value: Branch 상태
- Notify Observer: On Value Change
- Observer Aborts: Self

Enum은 한 순간에 하나의 값만 가지므로 현재 실행 Branch의 Decorator가 false가 되면 Self Abort 후 Root Selector가 새 상태를 다시 선택한다.

Run Behavior Dynamic 설정:

| State | Injection Tag | Default Behavior Asset |
|---|---|---|
| Dead | AI.Behavior.Dead | BT_Subtree_Common_Dead |
| Frozen | AI.Behavior.Frozen | BT_Subtree_Common_Frozen |
| Combat | AI.Behavior.Combat | 기본 Combat Subtree 또는 None |
| Investigating | AI.Behavior.Investigating | BT_Subtree_Common_Investigating |
| Passive | AI.Behavior.Passive | BT_Subtree_Common_Passive |

Default Behavior Asset은 Behavior Set에 해당 State Override가 없을 때 사용하는 안전한 fallback이다.

Root BT 자체에 Move To, 공격, 순찰 노드를 직접 두지 않는다.

---

## 8. DA_EnemyBehavior_RangedGround 생성

1. Content Browser에서 `Miscellaneous > Data Asset`을 선택한다.
2. Data Asset Class로 `EnemyBehaviorSet`을 선택한다.
3. 이름을 `DA_EnemyBehavior_RangedGround`로 지정한다.
4. `State Behaviors` 배열에 필요한 Override를 추가한다.

권장 설정:

| State | Injection Tag | Subtree |
|---|---|---|
| Passive | AI.Behavior.Passive | BT_Subtree_RangedGround_Passive |
| Combat | AI.Behavior.Combat | BT_Subtree_RangedGround_Combat |

Investigating, Frozen, Dead는 공통 Root Dynamic Node의 Default Behavior Asset을 그대로 사용할 수 있으므로 배열에서 생략할 수 있다.

한 Behavior Set 안에 같은 State 또는 같은 Injection Tag를 두 번 넣으면 첫 항목만 적용되고 Warning이 출력된다.

---

## 9. BP_RangedEnemy 설정

`BP_RangedEnemy` Class Defaults에서 다음을 확인한다.

### 9.1 AI

| 속성 | 값 |
|---|---|
| AI Controller Class | `RangedEnemyAIController` 또는 파생 BP Controller |
| Auto Possess AI | Placed in World or Spawned |
| Behavior Tree | `BT_EnemyBase` |
| Behavior Set | `DA_EnemyBehavior_RangedGround` |

기존 가이드의 “Behavior Tree를 지정하지 않는다” 설정은 더 이상 사용하지 않는다. Controller의 10Hz 자동 공격 루프가 제거되었으므로 BT를 지정하지 않으면 Perception은 동작하지만 행동은 실행되지 않는다.

### 9.2 이동

| 속성 | 권장 시작값 |
|---|---:|
| Set Movement Speed / Idle | 0 |
| Set Movement Speed / Jog | 350 |
| Set Movement Speed / Strafe | 250 |
| Set Movement Speed / Run | 500 |
| Character Movement / Orient Rotation to Movement | Off |
| Use Controller Rotation Yaw | On |

전투 중 회전은 Sequence의 `Set Focus` Task가 시작하고 `Clear Focus` Task가 해제한다.

### 9.3 공격

기존 설정을 유지한다.

- Min Attack Range
- Max Attack Range
- Attack Cooldown
- Projectile Speed
- Projectile Class
- Attack Montage
- Arrow Socket

사거리와 Cooldown을 BT 노드에 다시 복사하지 않는다. C++의 RangedEnemy 값을 기준으로 Decorator와 Task가 판단한다.

---

## 10. 선택 사항: BP_RangedEnemyAIController

Enemy 종류별 감각 수치가 필요하면 `RangedEnemyAIController`를 부모로 Blueprint Controller를 만든다.

기본 Ranged 값:

| 설정 | 기본값 |
|---|---:|
| Sight Radius | 3000 |
| Lose Sight Radius | 3500 |
| Peripheral Vision Degrees | 80 |
| Sight Max Age | 2 |
| Hearing Range | 500 |
| Hearing Max Age | 3 |
| Damage Max Age | 5 |

`BP_RangedEnemy`의 AI Controller Class를 새 Controller BP로 바꾼다.

감각별 숫자는 Controller Defaults에서 조정하며 C++ Handler나 BT Task에 숫자를 하드코딩하지 않는다.

---

## 11. Hearing 이벤트를 연결하는 방법

이번 리팩터링에는 Player 입력을 추가하지 않았다. 실제 Gameplay 코드에서 소음이 발생하는 지점이 다음 API 중 하나를 호출해야 한다.

```text
UAISense_Hearing::ReportNoiseEvent
Pawn MakeNoise
```

적합한 후보:

- 달리기 발소리 Anim Notify
- 총기/활/대포 발사
- 폭발
- 무거운 오브젝트 충돌

`Play Sound`만 호출해서는 AI Hearing 자극이 발생하지 않는다.

Noise Instigator는 실제 플레이어 또는 원인 Actor로 지정하고, Noise Location은 사건이 발생한 월드 위치를 사용한다.

---

## 12. AI Debugger 검증

PIE에서 다음 순서로 검증한다.

1. Enemy를 선택하거나 바라본다.
2. `'` 키로 AI Debugger를 연다.
3. Perception 카테고리를 활성화한다.
4. Blackboard에서 `State`, `TargetActor`, `PointOfInterest`를 확인한다.

### 12.1 시작

예상값:

```text
State = Passive
TargetActor = None
PointOfInterest = Not Set
```

### 12.2 Sight

Player가 시야에 진입:

```text
State = Combat
TargetActor = Player
Combat Dynamic Subtree 실행
```

Player가 시야에서 사라지고 다른 표적이 없음:

```text
TargetActor = None
State = Passive
```

### 12.3 Hearing

Gameplay 시스템에서 Noise Event 발생:

```text
State = Investigating
PointOfInterest = Noise Location
```

조사 이동 중 Player 발견:

```text
Investigating Branch 즉시 Abort
State = Combat
Combat Subtree 실행
```

### 12.4 Damage

실제 GAS Damage를 Enemy에게 적용:

```text
Health 감소
Damage Sense 자극
TargetActor = Damage Instigator
State = Combat
```

Instigator가 없는 환경 피해는 임의의 전투 표적을 만들지 않는다.

### 12.5 사망

```text
State = Dead
TargetActor Clear
PointOfInterest Clear
이동 중지
기존 Death GA / Ragdoll / Drop 실행
```

---

## 13. 지상 RangedEnemy 수동 테스트

### A. 사거리 밖

1. Player를 Max Attack Range 밖에 둔다.
2. `Can Ranged Attack`이 false인지 확인한다.
3. Reposition Task가 NavMesh 위 위치를 요청하는지 확인한다.
4. 공격 가능 거리 도달 후 Attack Branch로 전환되는지 확인한다.

### B. 너무 가까운 표적

1. Player를 Min Attack Range 안으로 이동한다.
2. 즉시 발사하지 않는지 확인한다.
3. Strafe/Reposition이 Desired Radius 방향으로 이동시키는지 확인한다.

### C. LOS 차단

1. Visibility를 Block하는 벽을 사이에 둔다.
2. 발사하지 않는지 확인한다.
3. Reposition 후 LOS가 확보되면 발사하는지 확인한다.

### D. Cooldown

1. Attack Cooldown을 2초로 둔다.
2. 첫 공격 뒤 BT Task가 Cooldown을 기다리는지 확인한다.
3. 별도 BT Cooldown Decorator 없이 약 2초 간격으로 발사하는지 확인한다.

### E. State Abort

1. Combat 중 `Frozen`으로 변경한다.
2. Combat Task와 GA가 Abort/Cancel되는지 확인한다.
3. 이동이 중단되는지 확인한다.

---

## 14. 이후 DeckRangedEnemy 확장 규칙

권장 상속:

```text
ABaseEnemy
  ARangedEnemy
    ADeckRangedEnemy
```

재사용:

- `EEnemyAIState`
- Root `BT_EnemyBase`
- Sight/Hearing/Damage 처리
- TargetActor 계약
- `Can Ranged Attack`
- `Ranged Attack`
- `UGA_RangedEnemyAttack`
- Projectile

교체:

- Passive Subtree: 갑판 로컬 순찰
- Investigating Subtree: PointOfInterest를 갑판 로컬 위치로 변환
- Combat Reposition: NavMesh 대신 Deck Local Path
- Frozen/Dead는 공통 사용 가능

Deck 전용 최우선 `Recover`가 필요해지면 다음 두 방법 중 하나를 선택한다.

1. `EEnemyAIState`에 `Recovering`을 새 숫자로 추가하고 Root Branch 추가
2. Deck Combat/Passive Subtree 내부에 `IsOutsideDeck` 우선 Selector 추가

Recover가 Combat보다도 우선하는 독립적인 고수준 상태라면 1번을 권장한다.

HostShip을 Projectile/LOS Ignore에 추가하는 처리는 BT가 아니라 RangedEnemy 또는 Deck Component의 C++ 확장점에서 처리한다.

---

## 15. 문제 해결

### State가 항상 Passive

- `BB_EnemyBase.State`가 Native Enum `EEnemyAIState`인지 확인
- Key 이름이 정확히 `State`인지 확인
- `BP_RangedEnemy.Behavior Tree`가 지정되었는지 확인

### Perception은 보이지만 공격하지 않음

- Behavior Set이 지정되었는지 확인
- Root Dynamic Node Injection Tag와 Data Asset Tag가 같은지 확인
- Combat Subtree Blackboard가 `BB_EnemyBase`인지 확인
- Projectile Class와 Ranged Attack GA가 등록되었는지 확인

### Dynamic Subtree가 실행되지 않음

- `Run Behavior`가 아니라 `Run Behavior Dynamic` 노드인지 확인
- Gameplay Tag가 문자열까지 정확히 같은지 확인
- Output Log의 `LogEnemyAI` Warning 확인
- 같은 Behavior Set에 중복 State/Tag가 없는지 확인

### 소리를 못 들음

- 단순 `Play Sound`만 실행한 것은 아닌지 확인
- `ReportNoiseEvent` 또는 `MakeNoise` 호출 여부 확인
- Noise Instigator가 유효한지 확인
- Hearing Range 안인지 확인

### 피해를 받아도 반응하지 않음

- 실제 Health가 감소했는지 확인
- Gameplay Effect Context에 Original Instigator 또는 Effect Causer가 있는지 확인
- 환경 피해처럼 Instigator가 null인지 확인
- Damage Sense Max Age가 0이 아닌지 확인

### 공격 Cooldown 중 Enemy가 멈춘 것처럼 보임

- `Ranged Attack` Task가 Cooldown을 기다리는 정상 상태일 수 있다.
- BT Debugger에서 해당 Task가 In Progress인지 확인한다.
- 이동과 공격을 병렬로 수행하고 싶다면 단일 Sequence가 아니라 별도 Service/Parallel 설계가 필요하다.

---

## 16. 자동화 검증

에디터 타깃 빌드:

```powershell
& "C:\Program Files\Epic Games\UE_5.7\Engine\Build\BatchFiles\Build.bat" `
  ArtisticSW2026Editor Win64 Development `
  "<ProjectRoot>\ArtisticSW2026.uproject" `
  -WaitMutex -NoHotReloadFromIDE
```

기존 RangedEnemy 회귀 테스트:

```powershell
& "C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" `
  "<ProjectRoot>\ArtisticSW2026.uproject" `
  -Unattended -NullRHI -NoSplash -NoSound -NoP4 `
  -ExecCmds="Automation RunTests ArtisticSW.Enemy.RangedEnemy;Quit" `
  -TestExit="Automation Test Queue Empty"
```

검증 항목:

- Controller에 Sight/Hearing/Damage Config 존재
- Enum 직렬화 값 유지
- HostShip 없는 RangedEnemy의 직접 공격 유지
- 즉시 발사 Projectile 생성
- Projectile payload와 Status Effect 회귀 없음
