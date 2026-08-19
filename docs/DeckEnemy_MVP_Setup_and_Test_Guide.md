# Deck Enemy MVP 설정 및 테스트 가이드

## 1. 구현 범위

이 MVP는 서버에서 EnemyShip마다 소규모 Enemy 풀을 미리 만들고 비활성 상태로 보관한다. `ANavalAIController`의 실제 Sight 성공 이벤트가 `Player` 태그를 가진 플레이어 함선을 처음 감지하면, 짧은 지연 후 풀의 Enemy를 갑판 Spawn Point에 순차 활성화한다.

Waypoint는 별도 Actor가 아니라 EnemyShip Blueprint 내부의 `DeckWaypointComponent`다. 반드시 `ShipDeckMesh` 아래에 붙인다. 따라서 파도에 의해 함선의 X/Y/Z와 Pitch/Yaw/Roll이 변해도 Point의 월드 위치가 자동으로 같이 변한다. 이동 태스크는 시작 시 월드 목표를 복사하지 않고 실행 중 매 프레임 Point의 최신 월드 위치를 읽는다.

서버만 풀 활성화, Waypoint 선택, 이동, 전투를 결정한다. Waypoint 컴포넌트와 경로 ID는 복제하지 않는다. 활성 Enemy의 기존 Actor/CharacterMovement 복제만 사용하고, 비활성 Enemy는 `DORM_DormantAll`, Tick/Collision/Brain off 상태로 둔다.

## 2. Enemy Blueprint 만들기

1. `ADeckRangedEnemy`를 부모로 `BP_DeckRangedEnemy_MVP`를 만든다.
2. 기존 RangedEnemy와 동일하게 Mesh, Anim BP, 공격 Ability/Projectile, Behavior Tree를 지정한다.
3. `AI Controller Class`는 기존 `ARangedEnemyAIController` 계열을 사용한다.
4. 기본 이동 속도는 BT의 `Move To Live Deck Waypoint` 노드에서 250 cm/s로 설정한다.
5. MVP에서는 사망 후 1.5초 동안 시체 상태를 유지한 다음 다시 비활성 풀로 돌아간다. 이 함선에서는 다시 자동 출격하지 않는다.

일반 `ARangedEnemy`가 아니라 반드시 `ADeckRangedEnemy`의 자식이어야 풀의 활성/비활성, 체력 초기화, Dormancy 제어가 적용된다.

## 3. EnemyShip Blueprint에 Waypoint 배치

1. EnemyShip Blueprint의 Components 패널에서 `ShipDeckMesh`를 선택한다.
2. 그 아래에 `Deck Waypoint Component`를 최소 4개 추가한다.
3. 갑판 바닥 높이에 Point를 놓는다. 캐릭터 Capsule 높이를 더해 놓지 않는다. Spawn 때 갑판 Trace 결과에 Capsule 반높이와 2 cm 여유를 자동 적용한다.
4. 각 Point에 겹치지 않는 `Waypoint Id`를 준다. 예: 0, 1, 2, 3.
5. `Linked Waypoint Ids`는 실제로 걸을 수 있는 바로 이웃만 넣는다. 링크는 양방향으로 작성한다. 예: 0→1,3 / 1→0,2 / 2→1,3 / 3→2,0.
6. Spawn에 쓸 안전한 Point 두 곳 정도만 `Can Spawn`을 켠다.
7. 순찰에 쓸 곳은 `Can Patrol`, 원거리 전투 위치로 허용할 곳은 `Can Use In Combat`을 켠다.
8. 각 Point의 `Min/Max Wait Time`으로 순찰 멈춤 시간을 조절한다. 기본은 0.5~2.0초다.

Point 아래 갑판이 `ShipDeckMesh`의 충돌 형상에 포함되어 있어야 한다. 초기화 로그에서 중복 ID, 없는 링크 ID, 잘못된 부모가 발견되면 `[DeckEnemyMVP]` 경고가 출력된다.

## 4. EnemyShip 풀 설정

EnemyShip Blueprint Class Defaults의 `Ship > Deck AI`에서 다음만 설정한다.

- `Enable Deck Enemy MVP`: true
- `Deck Enemy Class`: `BP_DeckRangedEnemy_MVP`
- `Deck Enemy Pool Size`: 2
- `Deck Enemy Sight Activation Delay`: 0.25
- `Deck Enemy Activation Interval`: 0.35
- `Max Deck Spawn Retries`: 3
- `Deck Spawn Retry Interval`: 0.5
- `Deck Enemy Random Seed`: 1337

기존 EnemyShip Blueprint에 영향을 주지 않도록 기능은 기본 off다. 풀 크기는 코드에서 1~8로 제한한다.

## 5. 최소 Behavior Tree 구성

기존 RangedEnemy Blackboard를 그대로 사용한다. 최소 필수 Key는 `State`와 `TargetActor`다. Deck 좌표를 담는 Blackboard Vector Key는 만들지 않는다.

Passive 분기는 반복 Sequence로 구성한다.

1. `Select Deck Waypoint` — Selection Mode: Patrol
2. `Move To Live Deck Waypoint`
3. `Wait At Deck Waypoint`

Combat 분기는 기존 공격 분기를 유지하고, 공격이 불가능한 경우의 재배치 Sequence만 다음처럼 둔다.

1. `Select Deck Waypoint` — Selection Mode: Combat, TargetActor Key: `TargetActor`
2. `Move To Live Deck Waypoint`
3. 기존 `Set Focus` / `Ranged Attack` 분기로 다시 평가

Combat 선택은 전체 경로 탐색을 하지 않는다. 현재 Point에 직접 연결된 후보 중 `Can Use In Combat`인 곳만 보고, 목표와의 중간 사거리에 가깝고 Visibility Trace가 열린 지점을 우선한다. Passive 선택은 선택지가 둘 이상이면 직전 Point를 제외하고 Seed 기반 무작위로 고른다. 이 두 규칙만으로 고정된 0→1→2→3 반복을 피한다.

기존 `Move To`나 월드 `GoalLocation` 기반 태스크를 Deck 분기에 사용하면 안 된다. 함선이 움직이는 동안 목표가 낡은 좌표가 되기 때문이다.

## 6. Sight 활성화 조건

갑판 풀은 다음 조건을 모두 만족한 최초 Sight 성공 이벤트에서 한 번만 출격한다.

- EnemyShip을 조종하는 `ANavalAIController`가 플레이어 함선을 실제 Sight로 감지함
- 감지 Actor가 `AShip` 계열이고 `Player` 태그를 가짐
- Enemy 함선이 아니며 `Enemy` 태그가 없음
- EnemyShip이 살아 있고 Deck Enemy MVP가 켜져 있음

기존 해상 AI의 0.2초 거리 기반 표적 갱신은 변경하지 않았다. 그것은 항해/함포 표적용이고, 갑판 Enemy 출격만 실제 Sight 이벤트를 사용한다.

## 7. PIE 검증 순서

1. Dedicated Server 또는 Listen Server + Client 2대로 PIE를 시작한다.
2. 플레이어 함선이 Sight 밖에 있을 때 갑판 Enemy가 보이지 않고 충돌하지 않는지 확인한다.
3. 플레이어 함선이 Sight에 들어오면 0.25초 후 첫 Enemy, 이후 0.35초 간격으로 두 번째 Enemy가 활성화되는지 확인한다.
4. 파도를 크게 하고 EnemyShip이 이동/회전하는 동안 Point와 Enemy가 갑판을 따라가는지 확인한다.
5. 순찰이 직전 Point로 즉시 왕복하지 않고 여러 링크를 바꾸어 선택하는지 확인한다.
6. 플레이어가 시야/사거리 밖이면 전투 Point로 한 칸씩 재배치하고, 공격 가능하면 기존 Ranged Attack을 수행하는지 확인한다.
7. Enemy를 처치해 1.5초 뒤 사라지고 같은 Actor가 Destroy되지 않은 채 비활성 풀로 돌아가는지 Server World Outliner에서 확인한다.
8. 플레이어가 Sight를 나갔다 다시 들어와도 새 Enemy가 계속 생성되지 않는지 확인한다.
9. `stat net`, `stat game`, Network Profiler로 비활성 풀에서 지속 Actor/Movement 업데이트가 없는지 확인한다.

현재 자동화 테스트는 다음 명령군에 있다.

`ArtisticSW.Enemy.DeckMVP`

## 8. MVP에서 의도적으로 제외한 것

- NavMesh를 함선마다 동적으로 다시 굽기
- EQS, A*, 긴 경로 탐색, Point 예약
- 갑판 밖 복귀/낙하 복구
- 사망한 풀 Enemy의 자동 재출격과 웨이브 재등록
- Waypoint 및 선택 결과 전용 RPC/복제
- 다수 Enemy의 자리 겹침 회피

갑판 이탈 방지는 계획대로 외부 Invisible Blocker와 Enemy 전용 Collision Profile로 처리한다. 위 항목은 실제 플레이 테스트에서 필요성이 확인된 뒤 다음 단계로 추가한다.

## 9. 코드에서 정한 MVP 기본값과 이유

- 함선당 Pool 2명, 상한 8명: 초기 Actor 복제 비용과 전투 밀도를 작게 유지한다.
- 최초 Sight 한 번만 출격: 반복 Spawn/Despawn와 웨이브 정책을 MVP에서 제거한다.
- 0.25초 준비 지연, 0.35초 출격 간격: 동일 프레임 활성화/복제 Burst를 피한다.
- Spawn Trace 실패 시 0.5초 간격 3회 재시도: 파도/초기 Physics 정착 중 일시 실패만 흡수한다.
- Seed 1337 + Ship 이름 + Pool Index: 서버에서 재현 가능한 순찰 변화를 만든다.
- 사망 표시 1.5초: 피격 결과를 볼 수 있게 하되 Ragdoll/장기 시체 복제를 제외한다.
- Spawn Capsule은 World Up 방향: Character Capsule을 파도 기울기에 맞춰 눕히지 않는다.
- Point 이동은 직접 연결된 이웃 한 칸만: 동적 NavMesh 없이도 갑판 위에서 예측 가능한 최소 이동을 보장한다.

이 값들은 모두 MVP용이며, 플레이 테스트 수치가 나온 뒤 Blueprint 기본값으로 조정할 수 있다.
