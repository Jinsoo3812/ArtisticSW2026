# Enemy 이동 중 충돌·겹침에 따른 Point 해제와 재탐색 계획

작성일: 2026-10-09 (한국 시간)  
상태: 계획안. 코드·Blueprint·레벨·충돌 설정 변경 없음.

## 1. 목표와 적용 범위

선택한 Point로 이동하는 Enemy가 Player 또는 물체에 막히거나 실제 blocking 겹침이 발생하면 해당 이동을 중지하고 **현재 Point·점유 예약·경로를 해제한 뒤 새로운 안전 목적지와 경로를 탐색**한다. 타깃 추적과 Combat 상태는 유지한다. 같은 Point/같은 막힌 구간의 반복 선택, 좌우 왕복, 재탐색 폭주를 함께 예방한다.

현재 코드 검토와 1차 적용 대상은 일반 Deck Enemy의 Combat 접근, 쿨다운 측면 이동, LOS 복구, Passive 순찰, Investigation 보행이다. 공통 Route를 쓰는 Boss의 일반 보행은 영향 검증 대상이며, 적용 여부를 설정으로 명시한다. Ground Enemy의 NavMesh/PathFollowing, Boss의 확정 Dash·Vanish·공격 Root Motion은 실행 구조가 다르므로 이 재탐색 흐름에 자동 연결하지 않는다. 필요하면 후속 단계에서 각 이동 방식의 별도 adapter를 설계한다.

여기서 충돌은 **Enemy의 이동 캡슐을 막는 접촉/침투**다. 바닥 접지, 정상 계단, 피해/감지용 overlap까지 모든 접촉을 이동 실패로 처리하지 않는다. 전방 이동을 실제로 막는 Player·물체 접촉과 침투는 빠르게 처리하고, 정상 진행 중인 가벼운 측면 접촉은 중단하지 않는 정책을 권장한다.

## 2. 현재 구현에서 확인한 내용

| 위치 | 현재 동작 | 보완 필요 |
| --- | --- | --- |
| `DeckWalkRouteComponent::TickRoute()` | 다음 경로 지점으로 AddMovementInput을 준다. 일정 거리 이상 가까워지지 않으면 ProgressTimeout으로 Failed가 된다. | Player/물체 blocking, 침투, 보행면 이탈, 일반 시간 초과를 구분하지 않는다. |
| `BTT_MoveToDeckWaypoint` | 실행·Tick·Abort에서 이동 종료를 처리한다. 실제 실패 시 `OnDeckMoveFailed()`와 Route 정리를 호출한다. | 막힘 정보를 정리 전에 보존하고, 정확히 한 번 Point 해제와 재탐색 요청을 전달해야 한다. |
| `ADeckEnemy::OnDeckMoveReached/Failed()` | `CancelCombatRoute()`로 Route와 Claim을 정리한다. | 정리 경로는 존재하지만 실패 구간 기억과 충돌 원인별 후속 행동은 없다. |
| `DeckEnemyNavigationComponent::SelectNearGoal()` | 목적지의 공간·점유를 검사하고 Player 캡슐과 겹치는 종점을 제외한다. | 종점이 비어 있어도 그곳까지 가는 경로가 Player나 동적 물체를 통과할 수 있다. |
| `DeckWalkSurfaceSampler::HasPassage()` | 그래프 생성 시 등록된 장애물에 대해 캡슐 통과 검사와 바닥 지지 검사를 수행한다. | 이후 들어온 Player/물체, 미등록 장애물, 현재 Actor 크기 차이를 런타임에 다시 확인해야 한다. |
| `IsSupportedSegment()` | 등록된 장애물의 샘플 지지·clearance를 확인한다. | 이동 중 동적 Pawn/물체에 대한 현재 충돌 검사와 동일하지 않다. |
| `DeckWalkGraph::FindPath()` | A*와 선택적 AllowedNodes 필터가 존재한다. | 런타임 장애물 우회용 입력과 막힌 edge 필터는 현재 상위 경로 API에 명시되어 있지 않다. |
| `AcceptPath()` | 경로와 진행 시간·거리 측정값을 초기화한다. | 계속 새 경로를 받아 실제로는 정체 중이어도 장기 실패 기록이 사라지지 않게 별도 관리해야 한다. |
| `BTT_SelectDeckWaypoint::HoldOrKeepRoute()` | 선택 실패 시 남은 Route가 있으면 Succeeded, 없으면 0.3초 대기 후 Failed다. | 충돌로 폐기한 Route를 재사용하지 않도록 성공·실패 계약을 바로잡아야 한다. |
| `BTT_MoveAroundDeckTarget::PlanNextSegment()` | 새 경로 실패 시 타깃 유효성만으로 true를 반환할 수 있다. | 겹침/막힘 후 이전 Route를 계속 실행하거나 대기를 정상 계획처럼 처리하면 안 된다. |

현재 판단은 Source 검토에 근거하며 이번 단계에서는 PIE로 증상을 재현하지 않았다. 실제 사용 Blueprint의 캡슐 크기·충돌 응답·BT 오버라이드와 물체의 충돌 상태는 구현 전 감사 항목이다.

## 3. 권장 책임 분리

아래 신규 이름과 결과 값은 구현 제안이다.

| 담당 | 책임 |
| --- | --- |
| Enemy 이동 캡슐/CMC 관찰 adapter | 실제 blocking hit·시작 침투·겹침 해소 결과를 관찰하여 이동 세대와 함께 기록한다. callback에서 BT나 Route를 직접 교체하지 않는다. |
| DeckWalkRouteComponent | 전방 검사·실제 진행 측정으로 막힘을 확정하고 `Blocked`와 상세 원인을 반환한다. 바닥 지지 및 실제 이동은 기존 CMC와 협력한다. |
| DeckEnemyNavigationComponent | 최근 막힌 Goal/edge/물체, 재탐색 시각·횟수를 관리하고 동적 장애물을 반영한 목적지·경로를 선택한다. |
| DeckWalkArea / Graph | 공간·캡슐 통과·지지·동적 노드/edge 필터를 적용한다. AI State와 타깃은 변경하지 않는다. |
| 이동 BT Task | 해당 이동의 종료·Point 해제·결과 전달을 맡는다. 실제 실패를 성공으로 바꾸지 않는다. |
| Controller / Combat | 공격 우선순위, 타깃 자격·인지 상실, 상위 상태 전환을 기존 정책으로 관리한다. |

막힘 결과에는 PlayerBlocked / PawnBlocked / GeometryBlocked / StartPenetrating / NoProgress / OffSurface / InvalidRuntime 등을 구분한다. OffSurface는 Point 재탐색만으로 해결할 수 없으므로 기존 보행면 이탈 복구 계획에 전달한다.

## 4. 막힘·겹침 감지 정책

### 4.1 이동 전 검사

- 현재 실제 캡슐 위치에서 다음 경로 구간으로 짧은 capsule sweep를 수행한다. 캡슐 크기·회전·충돌 profile/response는 실제 이동 캡슐과 일치시킨다.
- 전방 길이는 속도와 짧은 예측 시간을 기준으로 하며 과도하게 멀리 검사하여 통로 전체를 항상 막혔다고 판단하지 않는다.
- Enemy 자신·자신의 이동을 막지 않는 부착 무기 등은 제외하되, Target Player와 다른 Pawn은 이동 장애물 검사에서 제외하지 않는다.
- 접지 바닥과 같은 Mesh에 있는 난간을 구분한다. HostShip 전체를 Ignore하지 않는다.
- 선만 비어 있다는 이유로 캡슐이 통과 가능하다고 보지 않는다. 캡슐 공간·정상 단차/경사·바닥 지지를 함께 검사한다.

### 4.2 실제 이동 결과

- CMC의 실제 이동에서 발생한 blocking hit 및 `bStartPenetrating`, 캡슐 공간 질의로 확인한 실제 blocking overlap을 관찰한다.
- `OnComponentHit` / BeginOverlap만을 유일한 감지 수단으로 사용하지 않는다. 이벤트 설정·이동 경로에 따라 관측이 누락될 수 있으므로 CMC 결과와 명시적 질의를 함께 검토한다.
- 원래 이동 방향을 막는 접촉인지, 자연스러운 slide/StepUp 후에도 진행할 수 있는 접촉인지 구분한다.
- 확정된 전방 blocking 또는 blocking 침투는 다음 안전한 서버 업데이트에서 Point를 즉시 해제한다. 기존 1초 정체 timeout을 반드시 기다리지 않는다.
- 원인이 명확하지 않은 정체는 요구 변위 대비 실제 배 로컬 진행, 남은 경로 길이, 경로 cursor 변화로 확정한다. 세계 위치만 비교하면 배의 움직임을 Enemy 진행으로 오인할 수 있다.
- 목표까지의 직선거리 증가만으로 막힘을 확정하지 않는다. 정상 우회 경로에서는 잠시 멀어질 수 있다. 의미 있는 실제 진행과 경로 전진을 함께 본다.
- 속도 0으로 의도한 대기, Attack, 피격, Stun/Frozen, 스폰/룸 복원 중에는 일반 이동 정체 타이머를 누적하지 않는다.

### 4.3 제외·우선순위

| 접촉/상태 | 처리 |
| --- | --- |
| Player가 실제 전방을 막거나 캡슐 blocking 겹침 | Point 해제 후 Player를 피한 재탐색. 공격 가능하면 공격 의사결정을 우선한다. |
| 물체/다른 적에 실제로 막힘 | Point 해제 후 장애 구간을 피한 재탐색. 좁은 통로에서는 양보/대기 후보를 검토한다. |
| 정상 바닥·계단·경사·안전한 벽면 slide | 이동이 진행되면 유지한다. |
| Hurtbox·공격/감지 volume의 비blocking overlap | 이동 실패로 처리하지 않는다. |
| 공격이 이미 확정됨 | 일반 충돌 재탐색으로 공격을 취소하지 않는다. 일반 보행에만 연결한다. |
| 사망·풀 비활성·Host 파괴/변경 | 재탐색보다 기존 수명 정리가 우선한다. |
| 갑판 이탈·잘못된 난간 접지 | 안전 접지 복구로 전달한다. 정상 바닥이 없는 상태에서 그래프 경로만 재설정하지 않는다. |

## 5. Point 해제 → 재탐색 실행 순서

```text
전방 blocking / 실제 침투 / 정체 확정
  → 이동 실패 정보와 현재 Point·경로 구간 캡처
  → 일반 AI 보행 입력 중지
  → 현재 Point의 Claim과 Route 및 Goal 해제
  → 실패 구간 기억·재탐색 요청 기록
  → 현재 위치/바닥/타깃/장애물 재확인
  → 새 목적지·우회 경로 선택 및 공간 검사
  → 새 Point 예약·경로 설치
  → 이동 재개
```

1. 정리 전에 Host·Surface·Revision·이동 목적·현재 Goal·막힌 edge·blocker 약한 참조·접촉 위치/법선·시각·세대를 기록한다.
2. CMC의 이동 내부 callback에서는 기록과 중단 요청만 전달하고, 안전한 실행 경계에서 Route/BT를 정리한다. 이동 중 경로 배열을 바꾸는 재진입 문제를 피한다.
3. 해당 이동의 입력과 보행 속도를 중지한다. 진행 중 피격 Root Motion/외력을 일반 AI 정지 코드로 삭제하지 않는다. `DisableMovement()`를 겹침 해결책으로 사용하지 않는다.
4. `CancelCombatRoute()` 또는 같은 책임의 정리 API를 통해 **CombatGoal, LocalGoal, LocalPath, Claim, 이동용 Blackboard 값**을 함께 정리한다. Actor 타깃·Focus·Combat 상태는 유지한다.
5. 중복 hit·Task Abort·Tick 실패가 같은 세대에 여러 번 들어와도 정리와 재탐색 요청은 한 번만 수행한다.
6. 이미 충돌로 폐기한 경로는 계획 실패 시 복원하지 않는다. 기존 안전 경로 보존은 충돌이 없던 정상 재계획에만 허용한다.
7. 새 후보·경로를 검사하고 새 Point Claim을 획득한다. 준비 실패 시 새 Claim/임시 경로를 남기지 않는다.
8. 늦은 기존 Task callback이 새 Point를 해제하지 못하도록 이동 세대/소유권을 검증한다. Route 설치와 BT 수명 전환을 일관되게 처리한다.

충돌이 발생한 이동에서 Point를 계속 유지하며 힘으로 밀어붙이는 방식은 사용하지 않는다. 현재 점유를 해제하더라도 최근 실패 구간 기억과 재탐색 횟수 기록은 별도로 유지한다.

## 6. 재탐색 후보와 경로 선택

### 6.1 목적지 재선택

- 기존 Combat은 실제 현재 타깃의 추적 기준과 설정 TargetDistance를 다시 해석한다. 난간 위 Player 감지는 유지한다.
- 현재 막힌 Point는 첫 재탐색에서 잠시 제외한다. 동일 NodeIndex의 precise 위치·근처 위치로만 조금 바꾸어 같은 장애 경로를 반복하지 않도록 공간 범위도 기록한다.
- 바닥·Surface·캡슐 여유·외곽/구멍·Claim·허용 영역을 통과한 후보만 검토한다.
- 목표 Player뿐 아니라 다른 Player·적·동적 물체와도 캡슐 간격을 확보한다. 단순 목적지 평면 거리 검사 외에 실제 capsule overlap을 확인한다.
- 기본은 같은 층/같은 전투 영역이다. 다른 층은 현재 허용된 실제 보행 연결이 있을 때만 사용한다.
- Player의 한쪽을 돌아가는 후보를 우선 검토하되 방향을 매번 무작위로 뒤집지 않는다. 한번 고른 우회 측면을 일정 구간 유지하고 실패 증거가 있을 때 바꾼다.
- 원하는 거리·측면 이동 정책을 크게 벗어나는 접근 fallback은 별도 결과로 표현한다. 실패를 숨기려고 허용 오차를 무조건 키우지 않는다.
- Passive는 다른 안전 순찰 Point를 선택한다. Investigation/LOS 복구는 원래 조사/복구 의미를 유지하는 주변 후보를 찾고 임의의 먼 목적지를 선택하지 않는다.

### 6.2 경로 전체의 충돌 예방

종점만 새로 고르면 다른 Point로 향하면서 동일 Player/물체를 다시 통과할 수 있다. 따라서 **막힌 목적지와 막힌 경로 구간을 각각 피해야 한다.**

1. 주변 동적 blocker를 현재 HostShip 좌표로 해석하여 후보 노드·edge에 임시 제약/비용을 적용한다. 다른 배/움직이는 물체의 오래된 월드 위치를 고정된 로컬 장애물로 캐시하지 않는다.
2. 현재 Graph의 AllowedNodes 입력을 재사용할 수 있는지 검토하고, edge 차단이 필요한 경우 최소한의 필터를 추가한다. 같은 거리 띠 경로의 기존 필터와 교집합으로 적용한다.
3. 고정된 보행 그래프의 `bEnabled`를 Player 한 명 때문에 전역 변경하지 않는다. 개인 경로 질의별 동적 필터로 시작한다.
4. 새 경로의 실제 캡슐 통과 여유를 확인하고, 이동 중에는 가까운 다음 구간을 재검사한다. 위험 구간은 더 긴 부분 경로를 검증한다.
5. start 위치가 blocker 영향 범위에 있으면 출발 노드를 통째로 차단해 모든 탐색을 실패시키지 않는다. 실제 침투를 먼저 해소하고, 안전하게 벗어나는 edge만 허용한다.
6. 다른 Point라도 첫 진행 구간이 방금 막힌 edge와 같으면 우회 성공으로 취급하지 않는다.

동적 물체는 질의 필터/부분 검증으로 대응하고, 영구적으로 변경된 대형 정적 장애물은 지역/그래프 갱신 대상으로 구분한다. Player가 움직일 때마다 함선 전체 그래프를 Rebuild하지 않는다.

## 7. 이미 겹친 상태의 안전 처리

Point 해제는 캡슐 침투를 물리적으로 해결하지 않는다. 재탐색 전에 현재 캡슐이 실제로 움직일 수 있는지 확인한다.

- 정상 바닥·계단의 겹침 해소는 CMC 동작을 유지한다.
- Player/난간 겹침으로 상향 보정되어 난간에 올라가는 경우에는 앞선 캡슐 겹침 계획의 안전한 수평 탈출/결과 검증을 적용한다.
- 탈출 후보도 캡슐 공간·갑판 지지·외곽 여유를 만족해야 한다. 단순히 Z를 0으로 만들거나 충돌을 잠시 꺼 통과시키지 않는다.
- 안전한 탈출을 찾지 못하면 제한된 정지·재평가로 전환한다. 이미 보행면을 벗어난 경우에만 별도의 이탈 복구 정책을 적용한다.
- 이 일반 이동 계획에서 반복 순간이동이나 사망·처치 처리로 정체를 숨기지 않는다.

## 8. BT 성공·실패 계약과 공격 우선순위

앞선 목적지 선택 실패 분석의 방향을 선행 또는 같은 변경 단위로 반영한다.

| 결과 | BT/이동 처리 |
| --- | --- |
| 새 목적지·경로 설치 완료 | 선택 Task 성공 → Move 실행 |
| collision로 현재 경로 폐기 | 이동 Task 실패 + Blocked 원인 기록 → 재탐색 분기 |
| 새 계획 실패 | 실패 반환 → 기존 Wait/재시도 분기. 폐기한 Route를 성공 경로처럼 사용하지 않는다. |
| 정상 이동 중 교체 계획 실패 | 아직 안전하고 동일 목적의 경로만 유지 |
| 공격 준비 완료 | 기존 AttackReady 우선순위로 이동 중단 후 공격 |
| 타깃 유효하지만 공격·이동 가능한 위치 없음 | Combat/Focus 유지, 제한 대기/관찰 |

권장 1차 구현은 기존 BT의 실패→Wait→재선택 구조를 활용한다. Task 안에 별도 0.3초 대기와 BT Wait를 중복 추가하지 않는다. 충돌 재탐색용 공통 시간 gate를 모든 보행 선택 진입점에서 확인하여, 같은 Tick에 Cooldown→Combat 접근 등 여러 분기를 순회해도 실제 탐색은 한 번만 하게 한다.

`BTT_SelectDeckWaypoint::HoldOrKeepRoute()`와 `MoveAroundDeckTarget::PlanNextSegment()`가 실패를 성공/계속 진행으로 바꾸는 경로는 수정 대상으로 명시한다. 필요한 경우 내부 결과는 RouteInstalled / ReplanPending / NoSafeRoute / InvalidTarget 등으로 구분하되 BT의 Succeeded는 설치 완료일 때만 반환한다.

Player와 접촉해도 실제 사거리·LOS·무기 준비·쿨다운이 만족하면 AttackReady가 재탐색 대기를 중단할 수 있어야 한다. 공격이 이미 확정된 뒤에는 일반 blocking 기록으로 공격을 취소하지 않는다. 재탐색 횟수를 줄이려고 전투 상태를 강제로 Passive로 바꾸지 않는다.

## 9. 반복 실패·왕복·폭주 방지

### 실패 기억

- 최근 Goal의 NodeIndex/precise 좌표, Surface·Revision, 막힌 edge/영역, blocker 약한 참조·현재 형상을 함께 저장한다.
- 첫 실패는 해당 개체의 다음 탐색에만 적용한다. 반복 확인된 물체 장애는 필요할 때 함선별 짧은 공유 장애 캐시로 확대한다.
- Player/다른 적이 이동하거나 물체가 파괴되어 통과 가능성이 확인되면 제외를 빨리 해제한다. 단순 TTL 만료 후에도 실제 blocking이 남아 있으면 다시 통과시키지 않는다.
- 동일 Point 영구 blacklist는 사용하지 않는다. Revision 변경 시 옛 Node/edge를 폐기하고 현재 blocker의 공간 증거만 재해석한다.

### 재탐색 횟수·시간

- 동일 이동 세대의 중복 hit는 합친다. 각 충돌 이벤트마다 즉시 전체 탐색을 수행하지 않는다.
- 총 재탐색 시도와 정체 시간을 별도로 관리한다. `AcceptPath()`나 `ClearGoal()`로 매번 이 기록이 초기화되면 무한 재탐색을 막을 수 없다.
- 의미 있는 실제 로컬 진행·경로 통과가 확인되면 실패 예산을 회복한다. 새 경로를 설치했다는 사실만으로 성공으로 간주하지 않는다.
- 짧은 상한 내 대체 경로를 못 찾으면 안전한 위치에서 잠시 양보/관찰한다. 장애물 이동, Player 이동/지지 변경, 공간 확보 또는 느린 재검사 때 재개한다.
- 공격 판단은 계속 가능하게 하고, 이동 재탐색 대기가 공격 타이밍을 잠그지 않도록 한다.

초기 튜닝 후보: 첫 재탐색 간격 0.15~0.35초, 막힌 후보/구간 기억 0.5~1.5초, 의심 정체 확인 0.15~0.3초, 빠른 재탐색 3~5회 후 0.5~1초 관찰. 모두 확정값이 아니며 실제 속도·캡슐·함선 운동과 기존 BT Wait를 측정하여 정한다. 확정 전방 blocking/침투의 Point 해제는 이 유예와 분리하여 빠르게 수행한다.

## 10. 여러 충돌 원인을 줄이기 위한 추가 고려사항

| 항목 | 고려사항 |
| --- | --- |
| 실제 캡슐 크기 | Tier/최종 BP/scale별 실제 Radius·HalfHeight로 종점과 경로를 검사한다. 그래프 샘플의 기본 45/100 cm가 모든 적에게 충분한지 확인한다. |
| 충돌 응답 일치 | runtime sweep가 CMC와 다른 profile/channel이면 가짜 막힘/누락이 생긴다. Player·물체·난간·선체·무기의 응답을 감사한다. |
| 단순/복합 충돌 | 보행 그래프의 trace 방식과 실제 이동 충돌 사이의 틈·돌출·숨은 blocking 면을 확인한다. |
| 난간·외곽 | 캡슐 반경에 안전 여유를 더하고 바닥 지지도 검증한다. 난간 위 Player의 추적 투영을 Enemy 보행 허용으로 사용하지 않는다. |
| 문·기둥·상자·장식물 | 시각 크기와 blocking 형상이 다른 경우 정리한다. 통로·문턱·머리 여유 및 물체 생성/이동 후 경로 변화를 확인한다. |
| Player 통과 경로 | 종점에서 Player를 제외하는 것뿐 아니라 경로 corridor에서 실제 캡슐 여유를 확보한다. 좁은 곳은 무리한 통과 대신 양보한다. |
| 다른 적과 군집 | 종점 Claim은 중간 경로 예약이 아니다. 초기는 주변 캡슐의 수평 분산·좁은 edge 통행 우선권·짧은 양보로 대응한다. 모두 동시에 좌우 반전하지 않는다. |
| 통행 우선권 | 필요 시 활성화 세대/안정된 개체 ID로 우선권을 정하고 오래 기다린 적에게 양보하도록 한다. 무제한 통로 예약은 피한다. |
| 함선 운동 | 진행과 실패 구간은 배 로컬 좌표로 관리하고 sweep는 현재 월드 transform으로 수행한다. Base 이동·Enemy 상대 이동을 구분한다. |
| 접촉 반복과 모서리 | 순간적인 접촉 흔들림은 억제하되 실제 침투는 즉시 처리한다. 우회 측면 유지와 충분한 여유로 경계 왕복을 줄인다. |
| 공격/피격 수명 | 공격 commit, HitReaction, Stun/Frozen, 사망 Root Motion과 일반 보행 요청을 분리한다. |
| 풀·Host·룸 복원 | 실패 캐시·타이머·blocker 참조·세대를 반환/Host 변경/스냅샷 복원에서 정리한다. 이전 callback이 재사용된 Enemy를 이동시키지 못하게 한다. |
| 네트워크 | 서버가 충돌 판단·Point/Claim 해제·재탐색을 결정한다. 클라이언트가 별도 목적지를 고르지 않으며 기존 CMC 이동 복제를 유지한다. |
| 성능 | 매 적·매 Tick에 전체 경로/그래프를 검사하지 않는다. 주변 후보·짧은 전방 구간·제한된 재탐색과 함선별 예산을 사용한다. |

## 11. 단계별 구현 계획

| 단계 | 작업 | 완료 조건 |
| --- | --- | --- |
| 1. 재현/감사 | Player 정면 blocking, 물체 blocking, Pawn 겹침, 난간 겹침을 분리하고 실제 BP·캡슐·BT·충돌 설정을 확인 | 최초 장애 원인과 정체 timeout 이전 움직임 확인 |
| 2. 결과/수명 계약 | Blocked 원인·이동 세대·Point/Claim 정리·중복 요청 처리 및 Task 성공/실패 계약 정리 | 실패를 성공으로 실행하지 않음, 정리 한 번 |
| 3. 이동 중 감지 | 전방 sweep, 실제 CMC 접촉/침투, 로컬 진행 관찰 추가 | 의미 있는 blocking을 빠르게 판정하고 정상 바닥/slide를 유지 |
| 4. 재탐색 | 실패 Goal/edge 기억과 현재 동적 장애물 filter, 새 후보/경로 검사 | 같은 장애 corridor 반복 진입 방지 |
| 5. BT 연결 | 접근·쿨다운·LOS 복구·순찰·조사에 연결, 공통 재탐색 gate | 실패 후 한 번의 대기/재선택, 공격 우선순위 유지 |
| 6. 침투/군집 보강 | 필요한 겹침 탈출 및 분산/통로 양보 적용 | 여러 적/물체 사이에서 올라탐·왕복·교착 완화 |
| 7. 검증/튜닝 | 아래 행렬·성능·네트워크 및 기존 회귀 검증 | 수용 기준 충족 및 비용 확인 |

예상 검토 파일: `DeckWalkRouteComponent.{h,cpp}`, `DeckEnemyNavigationComponent.{h,cpp}`, `DeckWalkAreaComponent.{h,cpp}`, `DeckWalkGraph.{h,cpp}`, `BTT_MoveToDeckWaypoint.cpp`, `BTT_MoveAroundDeckTarget.cpp`, `BTT_SelectDeckWaypoint.cpp`, `DeckRangedEnemy.{h,cpp}`의 일반 DeckEnemy 수명, 필요한 CMC 관찰 adapter/세대/실패 타입, 테스트 및 에디터 진단 설정.

대형 BT 재작성·Ground 이동 전환·Enemy끼리 blocking 전역 제거·물리 설정 일괄 변경은 1차 범위가 아니다. 기존 BT의 fallback 설정은 실제 자산에서 확인하고, 부족한 경우에만 필요한 대기/재탐색 연결을 추가한다.

## 12. 검증 행렬과 수용 기준

### 재현·회귀

1. 정지 Player가 경로 중간을 막지만 목적지는 비어 있는 경우: Point/Claim 해제, 우회 목적지/경로 선택.
2. Player가 이동 중 경로에 들어오거나 이미 겹친 경우: 중복 정리 없이 해제, 안전 침투 해결, 재탐색.
3. 여러 Player·다른 적·이동 물체가 통로를 막는 경우: 타깃 아닌 blocker도 인식.
4. 고정 물체와 그래프 생성 후 생성/이동한 물체: 종점·중간 edge 차단을 각각 확인.
5. 좌우 양쪽 차단/좁은 통로/모서리: 양쪽 왕복 대신 제한 양보/관찰. 물체가 사라지면 재개.
6. 난간 위 Player·Player가 갑판으로 내려옴: 타깃 추적 유지와 실제 공격/경로 재평가.
7. 계단·경사·단차·벽면 slide·갑판 상하 운동: 정상 이동을 막힘으로 오인하지 않음.
8. Combat 접근·쿨다운·LOS 복구·Patrol·Investigation 각각의 의미 보존.
9. 공격 준비·확정 공격·피격·Stun/Frozen·사망·풀 반환·Host 변경·Revision 변경·룸 복원 경합.
10. Standalone, Listen+원격 Client, Dedicated+2 Clients, 지연/손실·관련성 복귀 및 다수 적.

자동화는 Point/Claim/Route 일관 정리, 같은 세대 hit 병합, 폐기 경로 미복원, 동적 node/edge 필터, 동일 corridor 재선택 억제, 재계획 후 실패 예산 유지, stale callback 차단 등 결함 위험이 높은 계약에 집중한다. 실제 캡슐 접촉·함선 물리·시각 끊김은 PIE 수동 검증을 병행한다. 계획 단계에서는 테스트를 생성/실행하지 않는다.

### 완료 기준

- 실제 blocking/침투 확정 후 현재 Point와 Claim이 다음 안전한 서버 업데이트에서 한 번 해제된다.
- 새 계획이 준비되지 않았는데 Move Task가 Succeeded를 받아 실행되지 않는다.
- 막힌 Goal/edge를 즉시 반복 선택하지 않으며 장애물 변화 후 다시 이용할 수 있다.
- 타깃·Combat·Focus·정상 공격 우선순위를 유지한다. 물체에 막혔다는 이유만으로 타깃을 잃지 않는다.
- 우회 불가능하면 무한 계획/좌우 왕복 대신 제한 대기/관찰하고 공간 변화 시 재개한다.
- 여러 적의 Goal/Claim 경쟁에서 누수·중복 점유·교착을 줄이고, 풀/Host 수명 변경 후 캐시가 남지 않는다.
- 정상 바닥·계단·slide·함선 운동·피격·확정 공격·사망 동작에 회귀가 없다.
- 서버 실제 경로와 클라이언트 이동이 수렴하며 반복 보정 떨림과 탐색 비용 증가를 측정해 허용 범위 안에 둔다.

## 13. 로그·관측과 참고

이동 세대, AI 상태/Task, 이동 목적, Host·Surface·Revision, Goal/edge, blocker 종류·컴포넌트, Hit.Normal/침투 깊이, 요구/실제 로컬 진행, Point/Claim 해제 이유, 새 선택 결과, 재탐색 횟수·대기 시각을 같은 시간축으로 기록한다.

개발 표시: 현재 경로, 해제 Point, 막힌 구간, 전방 capsule sweep, 동적 제외 구역, 새 경로를 구분한다. 성공적인 새 계획 수와 실제 탈출/진행 수를 함께 세어, 계획만 반복되는 상황을 감지한다.

- 프로젝트: 현재 Source/Enemy의 DeckAI/Task/CMC 연결, `docs/DeckEnemy_Combat_BT_Design_2026-10-04.md`, `docs/DeckEnemy_Rail_Tracking_and_Sight_Config.md`.
- 이전 분석: `DeckEnemy_Capsule_Penetration_and_Rail_BT_Analysis_2026-10-08.md`와 난간 올라탐/보스 이탈 복구 계획. 이번 문서는 보행 중 blocking Point 해제·재탐색의 실행 기준을 추가한다.
- [Epic: Traces Overview](https://dev.epicgames.com/documentation/unreal-engine/traces-in-unreal-engine---overview?lang=en-US): shape trace와 blocking/initial overlap 결과 의미.
- [Epic: CharacterMovement API](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/UCharacterMovementComponent): 접촉/겹침 관찰 지점 참고. 실제 hook 가능 지점은 설치된 UE 5.7 헤더에서 확인했다.
