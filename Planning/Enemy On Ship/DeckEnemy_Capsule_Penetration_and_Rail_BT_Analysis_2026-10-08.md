# Deck Enemy 캡슐 겹침 및 난간 추적 이후 BT 동작 분석

작성일: 2026-10-08. 코드·Blueprint·충돌 설정 변경 없음.

## 1. 분석 범위와 결론

현재 Source와 난간 추적 변경 커밋 `f6ca6709`를 그 부모와 비교했다. 설치된 UE 5.7의 CMC 및 MovementComponent 겹침 해소 구현도 확인했다. 기존 에셋 감사 결과와 BT export는 참고했지만, 최신 실행 중인 BT 자산과 런타임 증상을 이번 분석에서 직접 확인하지는 않았다. 사용자 관찰인 “캡슐 겹침이 원인”을 해결책의 우선 가설로 삼는다.

권장 방향은 다음과 같다.

- 난간 올라탐: 겹침을 만드는 충돌 관계/형상을 먼저 정리하고, 필요할 때 적 전용 이동 처리에서 **난간 접촉으로 발생하는 위험한 상향 겹침 해소**를 제한한다. StepUp 차단만으로 해결됐다고 보지 않는다.
- BT 이상: 난간 타깃을 계속 추적하는 기능은 유지하고, 공격 가능 위치가 없는 경우와 일시 경로 실패를 다른 결과로 처리한다. 위치 선택 결과·기존 경로 재사용·관찰 대기·재계획 조건을 함께 정리한다.
- 갑판 복귀 후 Combat이 끊기는 경우: 만료된 LOS 복구 상태가 유효한 현재 타깃을 지우는 Service 경로를 먼저 확인한다.

## 2. 캡슐 겹침이 난간 올라탐으로 연결되는 이유

로컬 엔진 `MovementComponent.cpp`의 `GetPenetrationAdjustment()`는 `Hit.Normal * (PenetrationDepth + PullBackDistance)`로 겹침에서 빠져나올 변위를 계산한다. 따라서 접촉 법선에 위쪽 성분이 있으면 위로 탈출하는 후보가 만들어진다.

`ResolvePenetrationImpl()`은 후보 위치에서 겹치지 않으면 sweep 없이 위치를 옮기는 경로를 가진다. 실패하면 sweep, 두 겹침 해소 벡터의 조합, 원래 요구 이동을 합친 방향 등을 시도한다. 피격의 일반 sweep가 있다고 해도 겹침 해소의 결과가 난간 위로 갈 수 있으므로 이동 원천과 실제 보정 결과를 구분해야 한다.

CMC의 `MaxDepenetrationWithGeometry/Pawn` 계열은 보정 벡터의 최대 크기를 제한한다. 위로 향하는 방향을 제거하는 설정이 아니다. Physics body의 `MaxDepenetrationVelocity`와도 다른 경로다. 현재 `ABaseEnemy`는 `bEnablePhysicsInteraction = false`를 설정하지만 이것은 CMC의 캡슐 blocking과 겹침 해소를 없애지 않는다.

### 무엇과 겹치는지에 따른 우선 해결

| 겹침 상대 | 우선 권장 | 유지해야 할 것 |
| --- | --- | --- |
| 다른 Deck Enemy의 캡슐 | 적끼리 blocking을 계속 요구할 필요가 있는지 판단한다. 불필요하면 적 전용 충돌 구분으로 적-적 blocking을 없애고 접근 목적지 점유와 이동 중 수평 separation으로 분산한다. | Player blocking, 바닥/난간 blocking, 피격·무기·화살 판정. 기존 목적지 Claim만으로 경로 중 겹침까지 해결되지 않으므로 separation이 필요하다. |
| Player 캡슐 | 갑자기 Pawn 전체를 Ignore로 바꾸지 않는다. 생존·접지 적의 Pawn 겹침에 대해 안전한 수평 탈출을 우선하고 실패 시 검증된 가까운 위치로 복구한다. | 근접 교전에서 Player와 적의 충돌 및 사거리 정책. |
| 난간/갑판 공유 Mesh | 난간 접촉의 충돌 형상을 점검한다. 경사진 돌출/작은 면 때문에 위를 향하는 탈출 법선이 생기는지 확인한다. 필요하면 적 전용 단순 수직 blocker/난간 의미 영역을 사용한다. | 갑판 바닥 지지, Player의 난간 이용, 다른 층·계단, 공격 LOS/화살의 의도한 충돌. |
| 바닥·계단 | 엔진의 정상 수직 겹침 해소를 보존한다. | 함선 상하 운동, 바닥 파고듦 탈출, 계단 보행. |

### 적 전용 보정 정책을 추가하는 경우

1. 현재 실제 갑판 접지, 생존, 정상 풀 활성, 올바른 HostShip을 먼저 확인한다.
2. 겹침 상대가 Pawn/난간이며 기본 보정이 무효 보행면이나 과도한 높이 상승을 만드는 경우만 개입한다.
3. 현 위치의 갑판 접평면에서 겹침을 벗어나는 후보를 제한적으로 탐색한다. 후보의 캡슐 공간·지지 구간·외곽 여유를 확인한다. 함선 Up과 실제 CMC 중력/접지 방향을 구분한다.
4. 정상 바닥/계단 접촉이나 의도적인 공중 이동은 기본 CMC 처리를 유지한다.
5. 수평 후보로 해결하지 못하면 겹친 상태를 그대로 유지하지 않고 마지막 안전 위치 주변의 검증된 후보로 한 번 복구한다.
6. `GetPenetrationAdjustment()`의 첫 벡터만 변경했다고 끝내지 않는다. 두 벡터 조합·원래 요구 변위·여러 접촉·최종 실제 위치를 함께 검증한다. 전역 `Adjustment.Z = 0`은 적용하지 않는다.
7. 난간 `CanStepUp/IsWalkable` 차단은 추가 예방으로 둔다. 겹침 해소로 이미 높은 위치로 이동한 문제는 별도로 막는다.

처음부터 큰 이동 컴포넌트 재작성은 하지 않는다. 단일 적-난간과 여러 적-Pawn을 분리 재현하여, 충돌 authoring으로 충분한지 먼저 판단한 뒤 필요한 적 전용 hook만 추가한다.

필수 재현 로그: 서버 캡슐 전후 위치, 접촉 상대/컴포넌트, `bStartPenetrating`, `PenetrationDepth`, Hit.Normal, 기본/제한/최종 보정, 실제 floor/Base, 요구 피격 변위. “Z 상승이 StepUp 전에 겹침 해소에서 발생했다”는 증거로 인과관계를 확정한다.

## 3. 난간 추적 변경 전후의 행동 차이

| 항목 | 변경 전 | 변경 후 |
| --- | --- | --- |
| 타깃의 바닥 판정 | Player 자신의 `ResolveActorOnDeck()` 필요 | 난간 지지·공중 투영·짧은 기억을 가진 TargetAnchor 사용 |
| 목표 높이 | Player의 실제 발 높이 | Player XY와 연관 갑판 바닥 Z를 결합 |
| 난간 타깃 목적지 | 보행면 실패로 선택이 끊길 수 있음 | 주변 보행 노드에서 사거리/LOS 점수를 평가 |
| 정확한 목적지 | 가능한 경우 precise floor 우선 | 난간·공중·기억/복구 모드에서는 precise 후보 우선 경로를 건너뜀 |
| 일반 경로 교체 실패 | 기존 경로 제거 | 유효한 기존 경로/Claim 보존 |
| 목적지 선택 실패 | 즉시 실패/정리, 경우에 따라 타깃 해제 | 경로를 유지하거나 0.3초 기다린 뒤 실패 |
| 쿨다운 측면 이동 실패 | Task 실패 | 타깃 유효성만으로 성공 처리하며 0.3초 후 재시도 |
| 난간 주변 측면 공간 부족 | 실패 | 반대 방향 및 넓은 허용 범위의 일반 접근으로 fallback |
| LOS 복구 만료 | 조사 전환 | SupportedObstacle이면 복구만 정리하고 Combat 유지 |

따라서 변경 후에는 과거에 일찍 끝났던 “보이지만 공격할 수 없는 Player” 상태가 오래 유지된다. 이는 추적 지속이라는 의도에 맞지만, 기존 BT가 그 상태를 안정적으로 표현하지 못하는 부분을 드러낸다. 아래는 코드에서 확인한 동작 및 발생 가능한 증상을 구분한 분석이다.

## 4. 확인한 문제 후보와 우선순위

### A. 공격 불가능해도 이동 목적지는 성공한다 — 우선 확인

근거: `DeckEnemyNavigationComponent.cpp:63–96`, `DeckEnemyCombatComponent.cpp:89–103`.

난간 타깃은 주변 노드를 점수로 고른다. LOS 차단과 실제 3D 사거리 초과는 점수 벌점이며 후보 거부 조건이 아니다. 반면 공격 시작은 실제 거리·동일 Surface·현재 증거·LOS를 모두 요구한다. 이동 선택의 성공과 공격 가능한 위치의 확보가 서로 다른 결과다.

예시: 위쪽 Player와 적 중심의 수직 차이가 H이고 무기 사거리가 R이면, 직선거리 판정상 허용 수평 거리는 `sqrt(R²-H²)`이다(H < R). H >= R이면 같은 갑판에서 수평 접근만으로 거리 조건을 충족할 수 없다. 정확한 피해 가능성은 무기 충돌/애니메이션과 LOS도 추가 확인해야 한다.

가능한 증상: 같은 안전 노드에 도착 → 공격은 OutOfRange/BlockedLOS → 다시 접근 선택 → 현재 노드를 다시 선택 → 이동 즉시 완료/대기 반복. 난간 높이 때문에 공격하지 않는 것은 정상일 수 있지만, 반복 경로 선택은 안정된 관찰 상태로 대체하는 편이 좋다.

권장: `AttackPositionFound`, `BestSafeApproach`, `NoAttackPosition`, `TransientQueryFailure`를 구분한다. 공격 가능한 후보를 우선하고, 도달 가능한 최선의 접근점에서 공격 불가능함이 확인되면 타깃을 유지한 채 관찰한다. Player 이동·높이/지지 변화·LOS 변화·점유 변화 때 재평가한다.

### B. 실제 높이/지지 종류 변화가 재계획 조건에서 사라진다 — 갑판 복귀 증상 우선

근거: `DeckCombatTargetResolverComponent.cpp:70–85`, `DeckEnemyNavigationComponent.cpp:196–210`.

Anchor는 Player의 실제 XY와 갑판 바닥 Z를 저장한다. 현재 경로 재계획은 이 값의 Surface·XY 이동·바닥 Z를 비교하며, 실제 Player 높이와 Anchor.Source의 변경은 비교하지 않는다. 따라서 같은 XY·같은 Surface에서 난간 위 Player가 갑판으로 내려와도 이동 재계획 조건은 변화가 없다고 볼 수 있다.

가능한 증상: Player가 갑판에 내려온 후에도 기존 난간용 목적지·허용 오차의 경로를 계속 따른다. 공격 판정은 실제 좌표를 읽으므로 AttackReady가 되면 공격으로 전환할 수 있으며 영구적으로 공격이 막힌다고 단정하지 않는다. 경로가 끝나거나 새 선택을 할 때 정상화될 수도 있다.

권장: 현재 지지 출처, 실제 높이 변화의 구간, 공격 위치 가용성 변화를 재계획 트리거로 추가한다. 실제 위치 매 프레임의 작은 흔들림에 반응하지 않도록 임계값·안정 시간을 둔다. LOS RecoveryRoute는 현재 `bRecoveryRoute`로 재계획을 건너뛰므로, 스냅샷 유지 정책과 지지 전환 시 종료/갱신 정책도 별도로 정한다.

### C. LOS 복구 만료 시 정상 갑판 타깃도 해제될 수 있다 — 명확한 정책 공백

근거: `BTS_MaintainDeckCombatFocus.cpp:35–53`, `DeckEnemyCombatComponent.cpp`의 `HasRecovery()` 3초 만료.

저장된 복구가 만료되고 공격이 확정 중이 아닐 때, 현재 Anchor.Source가 **SupportedObstacle일 때만** 복구를 정리하고 돌아간다. 현재 Player가 DeckFloor/AirborneProjection이면 같은 예외를 통과하지 못하고 `ClearCombatTarget(true)`와 `StartInvestigation(snapshot)`을 실행할 수 있다.

재현 조건: 난간 또는 갑판에서 LOS 복구가 저장됨 → 아직 복구를 성공적으로 정리하지 못함 → Player가 갑판에 내려오거나 점프 → 복구 만료 → 현재 증거가 있어도 조사 전환. 이전에도 일반 갑판 타깃에 이런 만료 정책이 있었지만, 난간 추적 변경으로 그 상태가 더 자주/오래 살아남고 Source 전환 문제가 드러날 수 있다.

권장: LOS 재배치 실패/만료와 타깃 상실을 분리한다. 현재 타깃의 자격·인지·영역 및 추적 증거가 유효하면 복구만 끝내고 Combat의 안전 접근/관찰로 돌아간다. 실제 인지/증거 상실 조건에서만 조사로 전환한다.

### D. 기존 경로 보존과 Task 성공 의미가 충분히 구분되지 않는다

근거: `BTT_SelectDeckWaypoint.cpp:66–102`, `BTT_MoveAroundDeckTarget.cpp:32–83`.

`HoldOrKeepRoute()`는 새 목적지 선택 실패 시 기존 경로 핸들이 유효하면 Succeeded를 반환한다. 현재 Task가 요구한 접근/LOS 복구/거리 정책과 기존 경로가 같은 의미인지 검사하지 않는다.

쿨다운 측면 이동의 `PlanNextSegment()`도 새 계획 실패 시 타깃 유효성만으로 true를 반환한다. 이전 Route를 보존하는 변경과 결합하면 “대기”라던 실행이 실제로는 이전 방향 경로를 계속 따라갈 수 있다. 방향 플래그는 바뀌었지만 실제 경로가 바뀌지 않는 상황도 생긴다.

권장: `Replaced`, `KeptCompatibleRoute`, `Holding`, `Failed` 결과를 구분한다. 타깃·Surface·경로 목적/거리 정책·목표 안전성이 호환될 때만 보존 경로를 실행한다. Holding에서는 실제 이동을 중지하고, 같은 후보에 도착한 뒤 반복 계획하지 않도록 한다. 안전한 이전 경로 보존 자체는 유지한다.

### E. 정밀 목적지에서 노드 중심으로 바뀌며 동작이 거칠어진다

근거: `DeckEnemyNavigationComponent.cpp:63–66`, `PlanTargetDistanceRoute()`의 반대 방향 및 350 cm 허용 fallback.

DeckFloor에서는 precise endpoint를 먼저 사용하지만 SupportedObstacle/AirborneProjection/RecentAnchor에서는 노드 후보를 우선한다. 기존 75 cm 간격 샘플 노드와 넓은 fallback 때문에 정확한 거리·일정한 측면 궤적 대신 가까운 다른 안전 위치를 선택할 수 있다. 현재 목표 유지 벌점 100은 거리 제곱 점수의 단위에서 10 cm 차이 수준이므로 큰 안정화 장치가 아니다.

가능한 증상: 난간 전후에 목표가 바뀌고 좌우 방향이 뒤집힘, 측면 이동 중 갑자기 접근/후퇴함, 모서리에서 같은 지점으로 되돌아감.

권장: 후보 안전성은 유지하면서 가능한 precise endpoint도 비교 평가한다. 측면 이동의 fallback 접근을 별도 결과/행동으로 표시하고, 기존 목적지보다 실질적으로 개선될 때만 교체하는 임계값을 둔다. 무조건 점수 계수를 키우기보다 거리·LOS·사거리 항의 우선순위를 명시한다.

### F. 0.3초 대기가 실패로 끝나면서 반복 분기가 보인다

근거: `BTT_SelectDeckWaypoint.cpp:105–108`.

추적 또는 목적지 선택이 실패하고 경로가 없으면 잠깐 InProgress로 대기하지만 0.3초 후 Failed로 끝난다. 안정적인 관찰 상태를 유지하는 대신 Selector의 다른 분기로 돌아갔다가 재진입할 수 있다. 정확한 반복 주기는 실제 BT의 Wait·Decorator와 함께 확인해야 한다.

권장: 공간상 공격 불가능은 관찰 상태로 표현하고, 일시 질의 실패에만 제한 재시도를 사용한다. 모든 실패를 없애거나 타깃을 즉시 지우지 않는다.

## 5. 권장 수정 순서

1. 실제 겹침 상대를 단일 적/다중 적 재현으로 구분하고 충돌 관계·형상을 정리한다.
2. 현재 증거가 있는 타깃을 LOS 복구 만료만으로 해제하지 않도록 Service 정책을 정리한다.
3. 지지 종류·실제 높이·공격 위치 가용성 변화에 대한 재계획을 추가한다.
4. 새 목적지 선택과 기존 경로 보존/대기를 구분하고, 공격 불가능한 난간 타깃의 관찰 결과를 만든다.
5. precise 후보, 목표 유지 기준, 측면 fallback 동작을 튜닝한다.
6. 필요 시 적 전용 안전한 겹침 해소 정책을 추가하고 사망·풀·Host 변경과 연결한다.

난간 추적을 전체 되돌리는 것은 우선 권장하지 않는다. Player를 계속 인지하는 기능과 공격/이동 의사결정을 분리하면 현재 기능을 유지하면서 문제 지점을 작게 수정할 수 있다. AttackReady의 abort를 일괄 Both로 바꾸는 것도 피한다. 현재 확정 공격은 범위 변동으로 취소하지 않도록 보호하므로 그 계약을 유지해야 한다.

## 6. 런타임 확인 항목

BT 분기/Task 결과, AI State, Blackboard와 Enemy의 Target, Anchor.Source/실제 Z/LocalCenter, EvaluateAttack 결과, Cooldown, 기존/신규 목적지와 경로 목적, 재계획 트리거, Recovery 저장/만료/정리 이유를 같은 시간축으로 기록한다.

- 난간 위 stationary Player: 사거리/LOS가 불가능하면 안정적 관찰, 가능하면 공격.
- 난간에서 같은 XY의 갑판으로 착지: Source/높이 전환으로 다시 판단하고 정상 추적/공격.
- LOS Recovery 저장 후 3초 전후 착지·점프: 유효 타깃이 복구 만료만으로 조사 상태로 바뀌지 않음.
- 난간 모서리에서 양쪽 측면 목적지 차단: 실제 Holding과 기존 경로 보존을 구분.
- Player가 처음부터 정상 갑판에 있는 대조군: 난간 전용 로직의 일반 전투 영향 확인.
- 여러 Enemy 캡슐 접촉 + 피격: 상향 보정과 목표 선택/Claim 경쟁을 동시에 관찰.

이번 분석에서는 빌드·PIE·자동화 테스트를 실행하지 않았다. 코드 분기와 이전 커밋 비교로 가능한 경로를 확인한 결과이며, 사용자가 관찰한 특정 증상과의 최종 인과관계는 해당 로그로 확인해야 한다. 과거 `.copy` BT export는 난간 변경 이전 자료이고 저장된 감사 JSON은 자산 로드 정보이므로, 최신 실행 자산의 모든 수치/abort 설정까지 확인한 것으로 간주하지 않는다.

## 7. 공식 참고

- [Epic: GetPenetrationAdjustment](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/GameFramework/UMovementComponent/GetPenetrationAdjustment?application_version=5.5): 실패한 이동의 겹침을 해소하기 위한 보정 계산. 상세 알고리즘은 설치된 UE 5.7 소스에서 확인했다.
- [Epic: CharacterMovement API](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/GameFramework/UCharacterMovementComponent?application_version=5.5): CMC 겹침 해소, 보정 거리 제한, StepUp·바닥 판정의 API 의미.
- [Epic: Behavior Tree Decorators](https://dev.epicgames.com/documentation/unreal-engine/unreal-engine-behavior-tree-node-reference-decorators): Observer Aborts의 범위. 실제 런타임 자산의 오버라이드는 별도 확인이 필요하다.
