# DeckEnemy 이동/전투 및 Boss Sight Spawn MVP 가이드

## 구현 요약

- 공통 갑판 적 구현은 `ADeckEnemy`가 담당한다.
- 기존 `ADeckRangedEnemy`는 Blueprint 자산 호환용 자식 클래스로 유지된다.
- 활성 DeckEnemy는 갑판에 고정 Attach되지 않는다. `ShipDeckMesh`를 CharacterMovement Base로 사용하며 live Deck Waypoint를 따라 이동한다.
- `Deck Combat Role`로 근거리/원거리 전투 위치 선택 정책을 구분한다.
- `UBossEncounterComponent`는 Item Box 상호작용 또는 Player Ship Sight 중 하나를 Spawn 트리거로 선택한다.
- Spawn, Point 예약, AI 상태 전환은 서버 권한에서만 실행된다.
- `DeckEnemySpawnerComponent`가 일반 DeckEnemy Spawn과 Boss를 포함한 모든 Deck Point 예약 상태를 단독 관리한다.

## DeckEnemy Blueprint 설정

### 원거리

기존 `BP_DeckRangedEnemy`를 계속 사용할 수 있다.

- Parent: 기존 `ADeckRangedEnemy` 또는 새 `ADeckEnemy`
- Deck Combat Role: `Ranged`
- AI Controller Class: `RangedEnemyAIController` 계열
- Behavior Set: 원거리 Passive/Combat Subtree
- 원거리 공격 Ability, 활, Projectile 설정 유지

### 근거리

`ADeckEnemy` 부모의 `BP_DeckMeleeEnemy`를 만든다.

- Deck Combat Role: `Melee`
- AI Controller Class: `BaseAIController` 계열
- Behavior Set: 근거리 Passive/Combat Subtree
- Equip Weapon On Spawn: 기존 근거리 Enemy 정책에 맞게 설정
- 근거리 무기 및 Basic Attack Ability 설정

현재 MVP는 기존 자산 호환을 위해 `ADeckEnemy`가 `ARangedEnemy`의 전투 기반 기능을 계승한다. Melee 역할에서는 원거리 전용 속성 및 Ability를 사용하지 않는다.

EnemyShip의 네이티브 `DeckEnemySpawnerComponent`에서 `Enable Spawning`을 켜고 `Spawn Composition`에
`Enemy Class + Count`를 추가한다. 근거리 3명, 원거리 2명이 필요하면 두 Entry로 각각 수량을 지정한다.
Spawner는 요청한 정확한 클래스의 Pool Actor만 활성화한다.

## 공용 Deck Point 수동 저작

Boss와 일반 DeckEnemy는 같은 Point 그래프를 사용한다. 자동 Grid가 부정확한 난간, 계단, 좁은 통로와 Boss 전투 공간은 `BP_EnemyShip`에서 수동 Point로 보완한다.

1. `BP_EnemyShip` Blueprint Asset을 열고 `ShipDeckMesh` 아래에 `DeckWaypointComponent`를 추가한다.
2. Point의 중심을 캐릭터 Capsule 높이가 아니라 실제 갑판 바닥에 놓는다.
3. 이동 가능한 통로와 Boss 전투 공간의 외곽/중앙에 Point를 배치한다.
4. 각 수동 Point에 `1..9999` 범위의 중복되지 않는 `Waypoint Id`를 직접 지정한다.
5. 실제로 이동 가능한 이웃 Point의 ID를 `Linked Waypoint Ids`에 양방향으로 입력한다. 예를 들어 1에서 2로 이동시킬 경우 Point 1에는 2, Point 2에는 1이 모두 필요하다.
6. 자동 Mesh 초안이 필요할 때만 `Generate Deck Waypoints From Deck Mesh`를 실행한다. 생성된 Point는 `10000` 이상의 ID와 갑판 지지면 기반 링크를 함께 받는다.
7. 적을 생성할 안전한 Point에만 `Can Spawn`을 켠다. Spawn Point는 `Can Use In Combat`과 유효한 링크가 모두 필요하다.
8. `Validate Deck Waypoints`를 실행하고 중복 ID, 단방향/잘못된 링크, 고립된 사용 Point가 0인지 확인한다.
9. Compile/Save 후 레벨에서는 이 BP 인스턴스만 사용한다. 레벨 인스턴스에서 별도로 Point를 생성하지 않는다.

`Generate Deck Waypoints From Deck Mesh`는 초안이 필요할 때만 사용한다. 생성된 Point를 손으로 이동한 경우 `Lock Generated Location`을 켜야 다음 생성에서도 위치가 유지된다. `Clear Generated Deck Waypoints`는 수동 Point를 보존한다.

## Behavior Tree

Passive Subtree는 다음 순서를 반복한다.

1. `Select Deck Waypoint` / Selection Mode = Patrol
2. `Move To Live Deck Waypoint`
3. `Wait At Deck Waypoint`

Combat Subtree의 최상위는 Selector로 구성한다. 공격 가능한 경우가 항상 왼쪽의 높은 우선순위이며, 재배치 분기는 오른쪽 fallback이다.

1. Attack Sequence: `Can Attack` Decorator / Observer Aborts = `Lower Priority` -> Focus -> Attack
2. Reposition Sequence: `Select Deck Waypoint` / Selection Mode = Combat -> `Move To Live Deck Waypoint`

이동 중에도 현재 위치에서 공격 가능 여부를 확인한다. 가능해지는 즉시 이동 Task는 성공으로 끝나며 다음 BT 평가에서 공격 Sequence로 전환된다. 타깃이 크게 움직이거나 Point 그래프가 갱신되면 현재 경로를 버리고 다시 선택한다.

Melee 역할은 목표에 가장 가까운 전투 Point를 선호한다. Ranged 역할은 `Min/Max Attack Range`의 중간 거리에 가까운 Point를 선호한다.

`Move To` Vector Key를 사용하지 않는다. 움직이는 함선에서는 `Move To Live Deck Waypoint`가 Point의 최신 Ship-relative 위치를 매 프레임 조회해야 한다.

## Combat 이동 책임과 Point 경쟁 방지

- `UDeckNavigationComponent`는 EnemyShip 단위의 ship-local Point 그래프 스냅샷과 최단 경로 탐색만 담당한다.
- `UDeckEnemyNavigationComponent`는 개별 Enemy의 전투 후보 산출, 경로 상태, 타깃 이동에 따른 재탐색을 담당한다.
- `UDeckEnemySpawnerComponent`는 Spawn뿐 아니라 점유, 다음-hop 예약, 최종 Combat Point 클레임의 유일한 소유자다.
- `UBTT_SelectDeckWaypoint`는 정책에 맞는 경로 계획을 요청하고, `UBTT_MoveToDeckWaypoint`는 live Point 추적과 경로 진행만 담당한다.

최종 전투 Point에는 soft claim을 먼저 잡아 여러 AI가 같은 지점을 동시에 선택하는 Point Racing을 막는다. 실제 이동 충돌은 다음 한 칸에만 hard reservation을 걸고, 도착할 때 점유로 커밋한다. 전체 경로를 미리 예약하지 않으므로 교차 경로가 서로를 장시간 막지 않는다. 이동 실패, BT abort, Pool 반환, 사망 시 예약과 클레임은 서버에서 함께 해제된다.

## Boss Sight Spawn 설정

`BP_EnemyShip`의 `BossEncounterComponent`에서 다음을 설정한다.

- Encounter Enabled: true
- Encounter Trigger: `Player Ship Sight`
- Boss Class: 사용할 `BP_Ship_BossEnemy`
- Boss Spawn Point Component: Components 패널에 배치한 정확한 `DeckWaypointComponent` 선택

`Boss Spawn Point Component`가 설정되지 않은 레거시 자산만 `Boss Spawn Point Id`를 fallback으로 사용한다. 선택한 Point는 `Can Use In Combat`이어야 하고, 다른 Actor가 점유하지 않은 상태여야 한다.

Sight 트리거에서는 Enemy Item Box가 없어도 된다. `NavalAIController`가 `Player` 태그의 플레이어 함선을 실제 Sight로 감지하면 서버가 Encounter 시작을 한 번만 요청한다.

보스의 초기 전투 대상은 감지된 Player Ship의 `RidingPlayer`다. 함선 조종 전환이 아직 끝나지 않아 `RidingPlayer`가 없으면 Encounter는 `Waiting`을 유지하고 다음 Sight 이벤트에서 재시도한다.

기존 박스 방식이 필요한 함선은 다음 설정을 유지한다.

- Encounter Trigger: `Item Box Interaction`
- Enemy Item Box Component: 사용할 Child Actor Component

## 네트워크 정책

- `ANavalAIController`의 Sight 처리와 Encounter 시작은 Authority에서만 수행한다.
- Encounter 상태와 Spawned Boss 참조만 복제한다.
- Deck Point 예약은 서버 전용이며 복제하지 않는다.
- Spawn/예약 Component 자체도 복제하지 않는다. 커밋된 Enemy Actor의 Pool 상태, Waypoint ID와 CharacterMovement 결과만 복제한다.
- DeckEnemy의 이동은 서버 CharacterMovement가 작성하고 simulated proxy로 복제한다.
- AI Controller, Blackboard, Behavior Tree는 클라이언트에서 실행하지 않는다.
- 동일 Sight/상호작용이 반복돼도 `Waiting -> Spawning` 전이가 한 번만 성공한다.

## PIE 확인 순서

1. Dedicated Server + Client 2대로 시작한다.
2. DeckEnemy가 Passive 상태에서 여러 Patrol Point를 이동하는지 확인한다.
3. DeckEnemy 자체 Sight에 Player가 들어오면 Combat으로 전환하는지 확인한다.
4. 원거리 적은 사거리/LOS Point로, 근거리 적은 가까운 Point로 재배치되는지 확인한다.
5. 파도와 함선 회전 중 이동 Base가 유지되고 Enemy가 갑판에서 이탈하지 않는지 확인한다.
6. Player Ship이 Enemy Ship Sight에 들어오면 선택한 Boss Spawn Point에서 보스가 한 번만 나타나는지 확인한다.
7. 두 클라이언트가 동시에 감지/상호작용해도 보스가 중복 Spawn되지 않는지 확인한다.
8. 기존 Item Box Encounter 함선이 이전 방식으로 계속 동작하는지 확인한다.

자동화 테스트 그룹:

- `ArtisticSW.Enemy.DeckMVP`
- `ArtisticSW.Enemy.BossMVP`
