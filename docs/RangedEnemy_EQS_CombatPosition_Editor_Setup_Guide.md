# RangedEnemy Combat Position EQS 설정 가이드

## 1. 구현 결과

생성된 EQS 에셋:

```text
/Game/GameplayAbilitySystem/Enemy/AI/EQS/EQS_RangedEnemy_CombatPosition
```

런타임 Context:

```text
UEnvQueryContext_EnemyCombatTarget
```

Context는 `TargetActor`라는 문자열을 직접 읽지 않는다. Query Owner가 `ABaseAIController`이면 Controller가 관리하는 현재 Combat Target을 사용하고, Query Owner가 Pawn이면 해당 Pawn의 `ABaseAIController`를 통해 같은 Target을 구한다. 따라서 Controller Blueprint에서 Blackboard Target Key 이름을 바꿔도 EQS 코드를 변경할 필요가 없다.

EQS는 다음 계약으로 구성되어 있다.

```text
Target 중심 Donut 후보 생성
-> Target 거리 500~2000 필터 및 먼 위치 가점
-> 현재 Enemy 위치와 최소 300 이격 및 가까운 다음 지점 가점
-> Querier에서 후보까지 Path Exist 필터
-> 후보에서 Target까지 Visibility Trace 필터
```

## 2. Named Parameter

모든 밸런싱 값은 Query Params Data Binding으로 노출되어 있다. 아래 값은 에셋 기본값이며 `Run EQS Query` BT Task의 Query Config에서 Enemy 종류별로 덮어쓸 수 있다.

| Parameter | 기본값 | 용도 |
|---|---:|---|
| `RangedEQS_InnerRadius` | 600 | Donut 생성 안쪽 반경 |
| `RangedEQS_OuterRadius` | 1800 | Donut 생성 바깥 반경 |
| `RangedEQS_NumberOfRings` | 4 | 생성 Ring 수 |
| `RangedEQS_PointsPerRing` | 16 | Ring당 후보 수 |
| `RangedEQS_MinAttackRange` | 500 | Target 거리 하한 필터 |
| `RangedEQS_MaxAttackRange` | 2000 | Target 거리 상한 필터 |
| `RangedEQS_MinRepositionDistance` | 300 | 현재 위치 재선택 방지용 최소 이동 거리 |
| `RangedEQS_ItemHeightOffset` | 100 | 후보 위치의 예상 발사 높이 |
| `RangedEQS_TargetHeightOffset` | 60 | Target 조준 높이 |

다른 원거리 Enemy는 동일한 EQS를 사용하면서 BT Task의 Query Config 값만 변경한다.

예:

```text
Archer
  InnerRadius = 600
  OuterRadius = 1800

Longbow
  InnerRadius = 1000
  OuterRadius = 2400

ShortRangeCaster
  InnerRadius = 400
  OuterRadius = 1100
```

## 3. EQS 에셋 확인

1. C++ 빌드 이후 Editor를 다시 실행한다.
2. Content Browser에서 다음 폴더로 이동한다.

   ```text
   Content/GameplayAbilitySystem/Enemy/AI/EQS
   ```

3. `EQS_RangedEnemy_CombatPosition`을 연다.
4. 최초로 열었을 때 Runtime Option을 기반으로 Graph가 자동 생성된다. Graph가 생성되면 한 번 저장한다.
5. Root 아래에 `Points: Donut` Option과 다음 네 Test가 있는지 확인한다.

   ```text
   Distance: Enemy Combat Target
   Distance: Querier
   Pathfinding
   Trace
   ```

## 4. Donut Generator 확인

`Points: Donut`을 선택하고 Details를 확인한다.

| 속성 | 설정 |
|---|---|
| Center | `Enemy Combat Target` |
| Inner Radius | Query Param `RangedEQS_InnerRadius`, Default 600 |
| Outer Radius | Query Param `RangedEQS_OuterRadius`, Default 1800 |
| Number of Rings | Query Param `RangedEQS_NumberOfRings`, Default 4 |
| Points Per Ring | Query Param `RangedEQS_PointsPerRing`, Default 16 |
| Use Spiral Pattern | On |
| Define Arc | Off |

Projection 설정:

| 속성 | 설정 |
|---|---|
| Trace Mode | Navigation |
| Project Down | 1024 |
| Project Up | 512 |
| Extent X | 50 |
| Post Projection Vertical Offset | 10 |

Navigation Projection은 후보가 NavMesh 위에 있는지 검사한다. 이것만으로 현재 Enemy에서 후보까지 경로가 존재한다고 보장하지 않으므로 Pathfinding Test도 유지한다.

## 5. Distance: Target 설정 확인

첫 번째 Distance Test는 Target과 후보 사이의 전투 거리를 판정한다.

| 속성 | 설정 |
|---|---|
| Distance To | `Enemy Combat Target` |
| Test Mode | Distance 2D |
| Test Purpose | Filter and Score |
| Filter Type | Range |
| Minimum | Query Param `RangedEQS_MinAttackRange`, Default 500 |
| Maximum | Query Param `RangedEQS_MaxAttackRange`, Default 2000 |
| Scoring Equation | Linear |
| Scoring Factor | 1.0 |
| Clamp Min/Max | Filter Threshold |

Linear 양수 점수이므로 500에 가까운 후보보다 2000에 가까운 후보가 높은 점수를 받는다. 실제 생성 반경은 600~1800이므로 공격 가능 경계에서 100~200cm 여유를 가진다.

현재 `ARangedEnemy::CanAttackTarget()`은 3D 거리를 사용한다. 평평한 지상에서는 EQS의 2D 거리와 사실상 동일하지만, 큰 높이 차가 있는 맵에서는 최종 `Can Ranged Attack` 결과가 다를 수 있다. 해당 맵에서는 EQS Test Mode를 Distance 3D로 변경하거나 공격 거리 정책을 2D로 통일한다.

## 6. Distance: Querier 설정 확인

두 번째 Distance Test는 같은 위치를 반복 선택하거나 한 번에 Target 반대편으로 이동하는 현상을 줄인다.

| 속성 | 설정 |
|---|---|
| Distance To | Querier |
| Test Mode | Distance 2D |
| Test Purpose | Filter and Score |
| Filter Type | Minimum |
| Minimum | Query Param `RangedEQS_MinRepositionDistance`, Default 300 |
| Scoring Equation | Inverse Linear |
| Scoring Factor | 0.4 |

300cm보다 가까운 후보는 탈락한다. 남은 후보 중에서는 현재 Enemy와 가까운 후보가 높은 점수를 받으므로, 적이 Target을 가로질러 반대편으로 이동하기보다 가까운 옆 지점으로 점진적으로 이동한다.

## 7. Pathfinding 설정 확인

| 속성 | 설정 |
|---|---|
| Context | Querier |
| Test Mode | Path Exist |
| Test Purpose | Filter Only |
| Filter Type | Match |
| Bool Value | True |
| Path From Context | True |
| Skip Unreachable | True |

검사 방향은 다음과 같다.

```text
현재 RangedEnemy 위치
-> 후보 PointOfInterest
```

부분 경로를 전투 위치로 허용하지 않으려면 뒤의 `Move To` Task에서도 Allow Partial Path를 끈다.

## 8. Trace 설정 확인

| 속성 | 설정 |
|---|---|
| Context | `Enemy Combat Target` |
| Test Purpose | Filter Only |
| Filter Type | Match |
| Bool Value | False |
| Trace From Context | False |
| Trace Mode | Geometry by Channel |
| Trace Shape | Line |
| Trace Channel | Visibility |
| Item Height Offset | Query Param `RangedEQS_ItemHeightOffset`, Default 100 |
| Context Height Offset | Query Param `RangedEQS_TargetHeightOffset`, Default 60 |
| Only Blocking Hits | On |
| Trace Complex | Off |

`Trace From Context = False`이므로 후보 Item에서 Target Context 방향으로 검사한다. `Bool Value = False`는 중간에 Blocking Hit가 없는 후보만 통과시킨다는 의미다.

EQS Trace와 `Can Ranged Attack`의 LOS 검사는 둘 다 필요하다.

```text
EQS Trace
  이동 전에 후보 위치의 예상 LOS를 검사

Can Ranged Attack
  이동 완료 후 실제 Muzzle Socket에서 최종 LOS를 검사
```

## 9. Blackboard 설정

`BB_EnemyBase`의 기존 `PointOfInterest` Key를 전투 위치 결과에도 재사용한다.

| Key | Type | 용도 |
|---|---|---|
| `PointOfInterest` | Vector | 상태별 관심 위치. Combat에서는 Run EQS Query의 최종 전투 위치 |

기존 `TargetActor`는 그대로 사용한다.

`PointOfInterest`는 상태에 따라 의미가 달라지는 공유 위치 Key다.

```text
Investigating -> 소리/마지막 인식 위치
Combat       -> EQS가 선택한 공격 이동 위치
```

두 상태 Subtree가 동시에 실행되지 않으므로 Key 충돌이 없으며, 위치 Blackboard Key를 Enemy마다 추가하지 않아도 된다.

## 10. RangedEnemy Blueprint 설정

`BP_RangedEnemy`의 전투 거리와 EQS 필터를 일치시킨다.

```text
Min Attack Range = 500
Max Attack Range = 2000
Target Aim Height Offset = 60
```

다른 Enemy가 같은 EQS를 다른 거리로 사용할 때는 다음 두 값도 반드시 같이 변경한다.

```text
RangedEnemy 실제 Min/Max Attack Range
Run EQS Query의 RangedEQS_Min/MaxAttackRange
```

둘이 다르면 EQS를 통과했지만 `Can Ranged Attack`에서 실패하거나, 실제 공격 범위 밖의 지점을 선택할 수 있다.

## 11. Combat Behavior Tree 연결

`BT_Subtree_RangedEnemy_Combat`을 열고 다음 구조를 만든다.

```text
Root
└─ Selector
   ├─ Sequence: Reposition And Attack
   │  ├─ Decorator: TargetActor Is Set
   │  ├─ Set Focus: TargetActor
   │  ├─ Run EQS Query
   │  ├─ Set Movement Speed: Strafe
   │  ├─ Move To: PointOfInterest
   │  ├─ Set Movement Speed: Idle
   │  ├─ Ranged Attack
   │  │  └─ Decorator: Can Ranged Attack
   │  └─ Clear Focus
   │
   └─ Sequence: No Combat Target
      ├─ Decorator: TargetActor Is Not Set
      ├─ Set Movement Speed: Idle
      ├─ Clear Focus
      └─ Wait: 0.2~0.5
```

`Can Ranged Attack`을 `Reposition And Attack` Sequence 맨 앞에 배치하지 않는다. 현재 위치에서 공격 불가능하면 EQS까지 실행되지 않아 기존 정지 문제가 다시 발생한다. Decorator는 `Move To`와 `Idle` 뒤의 `Ranged Attack` Task에 배치해 실제 발사 직전에만 최종 거리와 LOS를 검증한다.

### Run EQS Query Task

| 속성 | 설정 |
|---|---|
| Query Template | `EQS_RangedEnemy_CombatPosition` |
| Run Mode | Random Best 25% |
| Blackboard Key | `PointOfInterest` |

Query Template을 지정하면 Details의 Query Config에 `RangedEQS_*` Parameter가 표시된다. 기본형 RangedEnemy는 에셋 기본값을 그대로 사용해도 된다. 파생 Enemy는 이 Task에서 필요한 값만 덮어쓴다.

### Move To Task

| 속성 | 설정 |
|---|---|
| Blackboard Key | `PointOfInterest` |
| Acceptable Radius | 50~100 |
| Observe Blackboard Value | On |
| Allow Strafe | On |
| Use Pathfinding | On |
| Allow Partial Path | Off |

## 12. 디버깅 순서

1. NavMesh 표시(`P`)로 Target 주변 600~1800 범위에 이동 가능한 영역이 있는지 확인한다.
2. PIE에서 RangedEnemy가 Player를 감지하도록 한다.
3. Behavior Tree Debugger에서 Combat 상태의 `PointOfInterest`가 갱신되는지 확인한다.
4. AI Debugger의 EQS 표시 또는 Visual Logger에서 후보별 탈락 이유를 확인한다.
5. 녹색 후보 중 바깥 Ring이 높은 점수를 받는지 확인한다.
6. 벽 뒤 후보가 Trace에서 탈락하는지 확인한다.
7. NavMesh가 끊긴 후보가 Pathfinding에서 탈락하는지 확인한다.
8. 이동 완료 후 `Can Ranged Attack`이 true가 되고 발사하는지 확인한다.

## 13. 튜닝 가이드

적이 너무 자주 짧게 움직이면:

```text
RangedEQS_MinRepositionDistance 증가
또는 PointsPerRing 감소
```

적이 너무 크게 횡단하면:

```text
Querier Distance Scoring Factor 증가
또는 Run Mode를 Random Best 5%로 변경
```

항상 바깥 Ring만 선택해 장애물 대응이 나쁘면:

```text
Target Distance Scoring Factor 감소
```

EQS 비용이 높으면:

```text
NumberOfRings 4 -> 3
PointsPerRing 16 -> 12
```

기본 구성은 64개 후보를 생성하며 Distance에서 먼저 후보를 정리한 뒤 Pathfinding과 Trace를 수행한다.
