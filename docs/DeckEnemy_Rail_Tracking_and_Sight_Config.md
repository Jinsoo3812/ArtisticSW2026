# Deck Enemy / Boss 난간 추적과 Sight 설정

작성일: 2026-10-07

## 적용한 변경

Player의 실제 위치, 갑판상의 추적 중심, 적의 이동 목적지를 분리했다. `DeckCombatTargetResolverComponent`를 Deck Enemy와 Ship Boss에 추가하고 이동·공격 시작·보스 목적지·스냅샷·경보에서 공통으로 사용한다.

- 같은 함선의 난간/장애물에 접지하면 CurrentFloor의 지지 컴포넌트와 지지점을 확인하고, 주변 갑판 Surface와 연관시켜 추적 중심을 갱신한다. 현재 지지가 확인되는 동안에는 기억 유예 시간이 적용되지 않는다. 처음부터 난간 위에 있는 Player도 처리한다.
- 난간과 갑판이 하나의 Mesh인 경우 수직 질의가 난간 윗면을 먼저 맞힐 수 있다. 그 높이에 대응하는 보행 노드가 없으면 제한된 범위의 주변 갑판 노드로 추적 관계를 찾는다.
- 추적 중심에 보행 노드가 없어도 정상 거리 목표와 주변의 도달 가능한 후보를 탐색한다. 난간/LOS 복구 후보는 실제 사거리 오차, LOS와 기존 목적지 유지도 평가한다.
- 새 목적지 선택이 실패하면 기존의 유효한 경로와 점유를 유지한다. 안전한 경로가 없으면 짧게 관찰하고 재시도한다. 목적지 실패만으로 CombatTarget을 해제하지 않는다.
- 측면 이동은 반대 방향과 제한된 일반 접근을 시도한다. 안전한 접근점에 도착했거나 공간이 부족하면 대기할 수 있다.
- 일반 적과 Boss Walk는 Player의 로컬 이동에 따라 제한된 주기로 재계획한다. Boss의 확정 스킬/Busy 상태에서는 걷기 재계획을 하지 않는다.
- 공격은 실제 3D 거리, LOS와 무기 충돌을 유지한다. 높은 난간 때문에 무기가 닿지 않으면 접근·관찰하며, 투영 위치에 피해를 주지 않는다. Dash/Vanish의 경로·종점 안전 검증도 유지한다.
- 타깃 변경·풀 반환·사망·함선 변경에서 추적 캐시를 정리한다. 그래프 재생성 시 추적 중심의 로컬 좌표와 새로운 이동 핸들을 구분한다.

`ResolveActorOnDeck`의 엄격한 자기 바닥 판정은 그대로 사용한다. 추적 결과 `FDeckTargetAnchor`는 이동 핸들 `FDeckWalkLocation`과 별도 타입이다.

## 추적 설정

실제 사용하는 Enemy/Boss Blueprint에서 `DeckTargetResolver` 컴포넌트를 선택하고 `Deck AI → Tracking`을 수정한다.

| 설정 | 기본값 | 용도 |
| --- | --- | --- |
| Maximum Projection Height | 250 cm | 지지점에서 연관 갑판을 찾는 최대 높이 차이 |
| Maximum Projection Distance | 350 cm | 연관 갑판의 주변 노드를 찾는 XY 범위 |
| Missing Evidence Grace | 1 s | 현재 지지를 해석하지 못했을 때 마지막 중심을 잠깐 유지하는 시간 |
| Draw Tracking Debug | 꺼짐 | 실제 타깃 위치와 추적 중심의 연결선 표시 |

`Missing Evidence Grace`는 난간 체류 제한이 아니다. 높이가 250 cm를 넘거나 주변 보행 노드가 350 cm 밖에 있는 난간은 실제 구조에 맞춰 범위를 조정해야 한다. 범위를 과도하게 늘리면 다른 층과 구분하기 어려워진다.

같은 Mesh에 여러 층의 난간이 있고 공간 질의만으로 Surface를 구분하기 어렵다면 함선의 `DeckWalkAreaComponent → Ship → Deck Walk → Tracking → Tracking Support Regions`에 구역을 추가한다.

- Surface Id: 기존 보행면의 SurfaceId와 일치시킨다.
- Local Center / Local Extent: 함선의 DeckMeshComplex 좌표계로 난간 지지점을 포함하는 박스를 지정한다.
- 다른 SurfaceId의 구역이 겹치면 해석을 거절한다. 이 구역은 추적 관계만 지정하며 새로운 이동 경로를 만들지 않는다.

별도 난간 Actor는 함선의 기존 장애물 등록 방식 또는 함선 부착 관계로 구성해야 한다. 다른 함선/육지에 접지한 Player는 현재 함선의 난간으로 인정하지 않는다.

진단에는 `Draw Tracking Debug`와 콘솔 `Log LogDeckTargetTracking Verbose`를 사용할 수 있다. 로그에는 Observer, Ship, Target, Source, Surface, 실패 이유가 기록된다. `SupportedObstacle`은 현재 접지 증거이며 `RecentAnchor`는 일시적인 기억 상태다.

## Enemy Sight Config 수정 방법

1. 실제 SpawnPlan이 사용하는 Enemy/Boss Blueprint를 연다. Tier 자식 Blueprint가 사용되면 해당 자식의 오버라이드도 확인한다.
2. `Class Defaults → Enemy → AI → Perception`에서 값을 수정한다. Details에서 `Sight Radius`를 검색해도 된다.
3. Compile / Save 후 PIE를 다시 시작한다. 설정은 AIController가 Pawn을 Possess할 때 적용된다.

AIController의 Sight 컴포넌트 대신 Enemy Blueprint의 `PerceptionSettings`를 수정한다. 부모 Blueprint 값 변경은 자식이 해당 값을 오버라이드하지 않은 경우에 상속된다.

| 설정 | 현재 Deck BP 값 | 현재 Boss BP 값 | 의미 |
| --- | --- | --- | --- |
| Sight Radius | 3000 cm | 2500 cm | 처음 발견할 수 있는 거리 |
| Lose Sight Radius | 3500 cm | 3000 cm | 이미 발견한 대상의 시야 유지 거리. Sight Radius 이상으로 설정 |
| Peripheral Vision Degrees | 80° | 70° | 정면 기준 반각. 80°이면 전체 160° |
| Sight Max Age | 2 s | 3.5 s | 인지 자극의 기억 시간 |
| Auto Success Range From Last Seen Location | 500 cm | 500 cm | 마지막 발견 위치 주변의 자동 시야 성공 범위 |

위 값은 2026-10-07 저장된 Blueprint를 읽어 확인한 값이다. C++ 기본값과 Deck Blueprint의 저장값이 다르므로 C++ 기본값만 수정하면 기존 Blueprint 오버라이드에 반영되지 않을 수 있다. 이번 작업에서 Sight 수치는 변경하지 않았다.

`Sight Max Age`를 늘려도 시야 실패 후 반드시 그 시간 동안 CombatTarget이 유지되는 것은 아니다. 현재 Controller는 시야 실패 이벤트에서 타깃 전환/조사를 판단한다. `Auto Success Range`를 늘리면 차폐된 대상의 인지가 유지될 수 있지만 실제 공격 LOS는 별도로 확인한다. Sight 수치는 원하는 인지 범위에 맞춰 조정한다.

## 행동 트리와 확인 결과

현재 `DA_DeckMeleeEnemy_AI`, `DA_DeckRangedEnemy_AI`는 각각 `SubTree/DeckMelee`, `SubTree/DeckRanged`의 Combat 트리를 사용한다. 기존 C++ Task에 관찰/재시도를 반영했으므로 새 트리로 바꿀 필요는 없다. Boss의 기존 Selector/Move/Strafe Task에도 같은 정책을 적용했다. Blueprint/BT 자산은 재작성하지 않았다.

Editor Development 빌드가 성공했다. 자산을 읽어 16개 Deck/Boss Blueprint의 새 컴포넌트와 21개 행동 트리, 40개 관련 노드의 로드를 확인했다. 기존 T1/T2/T3 Ship Blueprint의 제거된 DeckNavigationComponent 직렬화 경고는 별도 자산 정리 항목이다. 자동화 테스트는 사용자 요청에 따라 작성하거나 실행하지 않았다.

## 남은 수동 확인

다음 항목은 아직 PIE에서 플레이 검증하지 않았다.

1. Melee/Ranged/Boss가 Player를 처음부터 난간 위에서 발견하고, 60초 이상 머물러도 기억 만료로 경로와 타깃을 잃지 않는지 확인한다.
2. 난간을 따라 이동하면 적들이 갑판 안쪽의 새 접근점/발사점으로 반응하는지 확인한다. 무기가 닿지 않는 높이에서는 안전한 대기가 정상이다.
3. 여러 적의 점유, 벽/구멍/갑판 외곽, 다층 난간, 별도 난간 Actor를 확인한다. 모호한 구역은 Tracking Support Regions를 설정한다.
4. 다른 배/바다로 이탈, 타깃 사망, 적 풀 반환, Boss Dash/Vanish/Strafe, 함선 이동, 서버와 클라이언트에서 기존 이탈·충돌·피해 정책이 유지되는지 확인한다.
