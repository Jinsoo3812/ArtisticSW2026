# 함선 보행면 2개: Waypoint 1 높이 기준 구현과 수동 검증

생성 알고리즘, 데이터 구조, 이동과 네트워크 처리의 상세 설명은 [보행면 구현 설명서](DeckWalk_Implementation.md)를 참고한다.

## 1. 현재 구성

`BP_EnemyShip`은 아래 보행면 2개를 사용한다.

| 보행면 ID | 역할 | 높이 결정 |
|---|---|---|
| `UpperDeck` | 처음 구현한 기존 면 | 기존 로컬 높이 범위 200~900 cm 및 Seed 유지 |
| `Waypoint1Deck` | 추가 면 | **WaypointId = 1의 배 로컬 Z**를 기준으로 주변 실제 바닥 선택 |

`UpperDeck`은 호환성을 위해 유지한 기존 식별자다. 이름으로 두 면의 실제 상하 순서를 판단하지 않는다. 예전 `LowerDeck` 설정과 포인트 연결은 `Waypoint1Deck`으로 이전했다.

이번에 읽은 BP의 `DeckWaypoint_1` 상대 Z는 **677 cm**다. 이 수치를 코드에 고정하지 않고, 서버 생성 시 실제 1번 컴포넌트의 월드 위치를 `DeckMesh_Complex` 로컬 좌표계로 변환해서 사용한다. 부모 변형, 함선 월드 위치와 회전이 반영된다.

추가 면에서 기준 포인트의 **Z는 층 선택**, 메쉬 충돌은 **바닥 높이·범위·장애물**을 담당한다. 기준점의 XY는 보행면의 면적을 제한하지 않는다. 기존 Visual Mesh와 물리 충돌 메쉬를 이동시키거나 새로운 평면 충돌을 추가하는 구현은 아니다.

## 2. 책임 분리

| 구성 | 책임 |
|---|---|
| `FDeckWalkSurfaceSettings` | 보행면 ID, 충돌 소스, 높이 모드, 기준 포인트, 허용 범위, 선택적 Seed 필터 |
| `FDeckWalkHeightResolver` | 고정 범위 또는 기준 포인트 Z를 공통 로컬 좌표로 해석. 설정/포인트 위치를 변경하지 않음 |
| `FDeckWalkSurfaceSampler` | 바닥 충돌 추출, 기준 높이에 가장 가까운 면 선택, 천장·울타리·인접 통로 검사 |
| `FDeckWalkGraph` | 연결 영역과 A* 길찾기. 액터/네트워크/스폰 정책을 참조하지 않음 |
| `UDeckWalkAreaComponent` | 서버 그래프 수명, 좌표 변환, 보행면/목적지/스폰 위치 조회, 발밑 판정과 디버그 |
| `UDeckWalkRouteComponent` | 적별 경로 진행, 목표 재탐색, 높이를 포함한 도착 판정, 정체 처리 |
| 기존 Spawner / Boss Encounter | 적 풀, 클래스/스탯, 등장 조건, 포인트 예약과 점유 |
| 기존 Navigation / BT / Boss Selector | 전투·순찰 목표 선택. 최종 위치/이동 가능성은 보행면에 질의 |
| CharacterMovement | 실제 이동, 접지, 움직이는 배의 Movement Base 및 이동 복제 |

```text
서버 BeginPlay → Waypoint 등록 → 보행면 Rebuild
  → HeightResolver에서 면별 높이 범위 확정
  → 충돌 샘플링 / 통과 공간 검사 / 연결 영역 구성
  → 적 풀 준비와 기존 등장 조건 처리
  → 포인트 XY + 소속 보행면으로 스폰/목표 위치 결정
  → 발밑 바닥에서 목표까지 경로 탐색
  → 서버 CharacterMovement 입력 → 클라이언트 이동 복제
```

## 3. BP에 적용한 설정

대상: `/Game/Blueprints/Ship/Enemy_Ship/Blueprints/BP_EnemyShip`.

### DeckWalkAreaComponent

| 항목 | UpperDeck | Waypoint1Deck |
|---|---|---|
| Height Mode | Local Height Range | Waypoint Reference Height |
| Floor Component Names | DeckMesh_Simple, DeckMesh_Complex | DeckMesh_Complex, DeckMesh_Simple |
| Trace Complex | 꺼짐 | 꺼짐 |
| Minimum / Maximum Floor Z | 200 / 900 cm | 이 모드에서는 미사용 |
| Height Reference Point Id | 미사용 | **1** |
| Height Below / Above Reference | 미사용 | **75 / 75 cm** |
| Seed Point Ids | 7, 10200, 10120 | **빈 배열** |
| Required | 켜짐 | 켜짐 |
| 표시 색 | 초록 | 하늘색 |

- `Require Deck Walk Area`: 켜짐.
- `Obstacle Component Names`: DeckMesh_Complex, DeckMesh_Simple. 소스 바닥 자체도 검사해 같은 메쉬의 천장을 포함한다.
- `Walking Connections`: UpperDeck ↔ Waypoint1Deck. 실제 물리적 보행 통로가 검증된 인접 노드만 연결한다.
- `Patrol Across Surfaces`: 꺼짐. 기본 순찰은 현재 면 안에서 수행한다.
- Cell Size 75 cm, Maximum Step Height 45 cm, Maximum Floor Slope Degrees 40°.
- Clearance Radius 35 cm / Half Height 90 cm. 현재 근접·원거리·보스 BP 캡슐의 최댓값이며 바닥 위 5 cm 여유도 검사한다.
- Minimum Region Cells 6. 너무 작은 분리 영역을 제거한다.
- Draw Debug Area / Draw Debug Connections: 기본 꺼짐.

현재 `SM_Ship_Complex`는 Use Complex Collision As Simple 설정이므로 상부의 Trace Complex가 꺼져도 해당 메쉬의 실제 삼각형 충돌이 사용된다. 이 에셋의 충돌 모드를 바꾸면 보행면 소스 설정도 확인한다.

추가 면은 두 충돌 컴포넌트 모두를 조회한다. 1번 높이의 실제 바닥을 어느 메쉬가 제공하든 높이를 기준으로 선택하며, Simple은 물리 보행에 사용하는 단순 충돌 형상을 사용한다.

### 높이와 포인트 해석 규칙

1. 추가 면의 탐색 범위는 `기준 로컬 Z - 75` ~ `기준 로컬 Z + 75`다. 기준 로컬 Z가 677이면 602~752 cm다.
2. 각 XY에서 이 범위 안의 **기준 Z에 가장 가까운 실제 바닥 하나**를 선택한다. 선택한 바닥에 머리 공간이 부족하면 셀을 제외한다. 다른 높이로 대체하지 않는다.
3. 명시한 기준 높이 범위는 추가 면이 우선 소유한다. 기존 면의 넓은 탐색 범위와 겹치더라도 동일 바닥을 기존 면에 중복 등록하지 않는다. 기존 면의 설정값과 Seed는 유지한다.
4. 추가 면의 Seed 10055/10125는 제거했다. 다른 Waypoint의 위치가 추가 면의 생성 영역을 제한하지 않는다. Seed가 빈 배열이면 크기·충돌 조건을 만족하는 영역을 모두 유지한다.
5. `Walk Surface Id = Waypoint1Deck`인 스폰/전투 포인트는 **XY만 목적지로 사용**한다. 최종 Z는 해당 보행면의 바닥에서 가져온다. 기존 포인트의 오래된 Z가 추가 면을 원래 높이로 되돌리지 않는다.
6. 실제 Player/Enemy의 소속 면은 캡슐 중심이나 목표 포인트가 아닌 **현재 발밑 충돌**로 판단한다.
7. 기준 포인트가 없으면 오류와 함께 생성을 중단한다. 다른 포인트나 Z=0으로 대체하지 않는다.

포인트 XY는 가장 가까운 유효 노드로 최대 250 cm 보정될 수 있다. 기존 면의 포인트 높이 허용치는 100 cm이며 추가 면은 자신의 기준 높이 범위를 사용한다. 새 포인트에는 Walk Surface Id를 명시한다.

### 스폰 구성

| 대상 | 포인트 | 보행면 |
|---|---:|---|
| BP_DeckMeleeEnemy / T1_Deck_Melee | **1** | Waypoint1Deck |
| BP_DeckRangedEnemy / T1_Deck_Ranged | 10200 | UpperDeck |
| 기존 보스 BP | 10120 | UpperDeck |

근접 적 스폰을 기존 10055에서 1로 옮겼고, 1번의 Can Spawn / Can Use In Combat을 켰다. 기준 포인트의 위치 자체는 수정하지 않았다. 기존 추가 면의 포인트들은 Waypoint1Deck에 연결했다.

보스 기존 설정: Encounter Enabled=true, Trigger=PlayerShipSight, Required Story Node=ReconQuestAccepted, Stop After Story Node=MiddleBoss1Defeated. 보스 소환과 순간이동 후보도 보행면으로 해석한 위치를 사용하며, 필수 보행면의 목적지를 찾지 못하면 원래 포인트 Z로 대체하지 않는다.

## 4. 네트워크와 계산 정책

- 기준 높이 확정, 충돌 샘플링, 그래프, A*, 스폰/예약 및 이동 입력은 서버에서 처리한다.
- 보행면은 BeginPlay와 명시적 Rebuild에서 계산한다. 배가 움직일 때는 저장된 로컬 좌표를 현재 배 변형으로 변환한다.
- 클라이언트에서 Rebuild가 호출되어도 스냅샷/리비전을 변경하지 않는다. 클라이언트의 보행면 컴포넌트 틱도 비활성화한다.
- 경로/노드를 매 프레임 RPC로 전송하지 않는다. 기존 CharacterMovement의 위치·Movement Base 복제를 사용한다. 클라이언트가 기준 Z로 적을 재배치하지 않는다.
- 경로 컴포넌트는 바닥 컴포넌트가 달라질 때만 SetBase를 호출한다. 가까운 포인트를 다시 연결할 때도 위치를 한 번씩 계산해 정렬 중 반복 탐색을 줄인다.
- Dedicated Server에서는 디버그 도형을 그리지 않는다. 기준 높이와 노드 상태는 로그로 확인한다.
- Rebuild는 이전 경로 핸들을 무효화한다. 진행 중인 경로는 실패 처리 후 BT에서 다시 선택한다.

이 정책은 불필요한 클라이언트 계산과 위치 보정을 피하기 위한 구조다. 실제 지연/손실 환경의 동작은 아래 수동 항목으로 확인해야 한다.

## 5. 에디터 수동 검증 순서

### A. 설정과 보행면 위치

1. 에디터를 재시작하고 BP_EnemyShip을 Compile한다. Components에서 DeckWalkAreaComponent를 선택한다.
2. Surfaces가 정확히 2개이고 위 설정과 일치하는지 확인한다. 이름에 숫자 1이 들어간 컴포넌트가 아니라 **Waypoint Id가 1인 컴포넌트**가 기준이다.
3. 두 바닥 컴포넌트의 Query Collision / Pawn Block을 확인한다. DeckMesh_Complex의 최종 Scale은 `(1,1,1)`이어야 한다.
4. `/Game/Level/EnemyTest/T1Ship_Lv1`의 배치된 함선이나 상속 BP가 옛 Surfaces/Spawn Plan 값을 덮어쓰는지 확인한다. Archetype이 스폰 구성을 바꾸는 경우 실행 중 값도 확인한다.
5. Draw Debug Area와 Draw Debug Connections를 켜고 1인 PIE를 시작한다.
6. Output Log의 `[DeckWalk]`에서 두 면 각각 `Nodes > 0`, `Ready=1`을 확인한다. 추가 면의 `ReferencePoint=1`, `ReferenceLocalZ`, `SamplingZ`를 확인한다.
7. 기존 면은 초록, 1번 높이의 추가 면은 하늘색이어야 한다. 기준점의 하늘색 십자에는 `Reference=1 Z=... Range=[..., ...]`가 표시된다. 실제 노드는 가시성을 위해 바닥보다 8 cm 위에 표시된다.
8. F8로 Eject해 두 높이를 직접 확인한다. Debug Surface Filter를 UpperDeck 또는 Waypoint1Deck으로 지정하면 한 면만 표시한다. 울타리/기둥/천장 내부에 노드가 생기지 않아야 한다.

필요하면 서버의 테스트용 BP에서 `Get Surface Height Range`, `Get Surface Node Count`, `Get Actor Surface`를 Print String에 연결한다. 클라이언트의 IsReady=false는 서버 전용 그래프 정책상 정상이다. 이 그래프는 Recast NavMesh와 별도이므로 P 키와 NavMeshBoundsVolume으로 표시하지 않는다.

### B. 기준점만 높이를 결정하는지

1. 테스트용 BP 사본에서 1번 Z를 변경하고 PIE를 다시 시작한다. 추가 면의 기준/탐색 범위가 따라 바뀌어야 한다. 바닥 충돌이 새 범위에 없으면 생성 실패가 정상이다.
2. 기준 1번을 제외한 Waypoint1Deck 포인트의 Z를 변경한다. 추가 면과 그 포인트의 실제 목적지 높이는 바뀌지 않아야 한다. XY를 변경하면 목적지는 달라진다.
3. 1번 ID를 바꾸거나 제거한다. Missing height reference 오류가 나야 한다. 다른 면에 자동 스폰되면 실패다.
4. 확인 후 사본을 폐기하거나 변경한 값을 복원한다.

### C. 스폰·순찰·추적

1. Player 함선을 적 함선의 인지 범위에 진입시킨다. Player 함선 Actor Tags에는 Player가 있고 Enemy가 없어야 한다. 캐릭터만 갑판에 두는 것은 함선 인지 트리거와 다르다.
2. 근접 적은 1번 기준 추가 면, 원거리 적은 10200의 기존 면에 등장해야 한다. 천장/울타리에 끼거나 다른 높이로 이동하면 실패다.
3. 보스는 ReconQuestAccepted 완료, MiddleBoss1Defeated 미완료인 테스트 캠페인에서 함선 인지를 발생시킨다. 10120에서 등장하고 Encounter State가 Active가 되어야 한다.
4. 별도 수동 테스트 레벨에서 조건을 준비하려면 서버 StoryFacadeSubsystem에 CompleteStoryNode(GameStarted) → CompleteStoryNode(ReconQuestAccepted)를 호출한 뒤 함선 인지를 발생시킨다. 기존 캠페인 초기화는 이 구현에 포함하지 않았다.
5. 개인 적의 시야에서 벗어나면 각 적이 자신의 면에서 순찰하는지 확인한다. 같은 면의 Player를 인지하면 유효 바닥을 따라 접근해야 한다.
6. 같은 XY 근처의 다른 높이로 Player를 옮겨 확인한다. 다른 면을 도착 지점으로 오인하면 실패다. 갑판을 관통하는 사격도 차단되어야 한다.
7. Player가 배 밖으로 나가면 갑판 추적 경로를 종료해야 한다. 점프 중 발밑 탐색 범위(아래 65 cm)를 벗어나면 추적 대상이 해제될 수 있다.
8. 보스의 걷기는 도달 가능한 포인트를 선택해야 한다. Vanish/Teleport는 기존 전투 능력의 별도 이동 정책이다.

### D. 연결 통로와 네트워크

1. 층간 노란 연결선은 실제 계단/경사로에만 있어야 한다. 위아래로 겹친 갑판을 수직으로 연결하면 실패다.
2. 연결에는 인접 셀 높이 차이 45 cm 이하, 기울기 40° 이하, 중간 지지면과 캡슐 통과 공간이 필요하다. 사다리/점프/낙하 링크는 지원하지 않는다.
3. 실제 통로를 따라 Player가 이동하면 추적 적도 통로로 이동하는지 확인한다. 기본 Patrol Across Surfaces=false에서는 순찰이 현재 면을 떠나지 않아야 한다.
4. 테스트 사본에서 Walking Connections를 비우면 다른 면으로 보행 경로가 생기지 않아야 한다. 연결 허용 항목만으로 통로를 생성하지 않는다.
5. 배를 이동·회전시키며 두 면의 접지, 피격 후 이동 재개, 풀 재활성화를 확인한다.
6. 2 Players / Listen Server에서 서버와 클라이언트 적 위치를 비교한다. 네트워크 에뮬레이션의 지연 100~150 ms, 패킷 손실 1%도 수동 확인한다. 반복적인 층 변경 보정이나 순간 이동이 없어야 한다.
7. Dedicated Server + 2 Clients, 늦게 접속한 클라이언트의 초기 위치와 Movement Base도 확인한다. Dedicated Server의 보행면 상태는 로그로 확인한다.
8. 확인 후 디버그 표시를 끄고 저장한다.

## 6. 실패 진단과 한계

| 증상 | 확인 사항 |
|---|---|
| Missing height reference WaypointId=1 | 실제 ID, 포인트 등록, 기준 컴포넌트 존재 여부 |
| 추가 면 높이가 예상과 다름 | ReferenceLocalZ 로그와 부모 변형, Above/Below 범위, 실제 충돌 바닥 |
| Required surface has no usable nodes | 소스 충돌, 기준 높이, 머리 공간, 캡슐 크기, 작은 분리 영역 |
| 첫 면까지 스폰되지 않음 | 두 면 모두 Required이므로 한 면 생성 실패도 전체 스폰을 차단함. `[DeckWalk]` 오류부터 해결 |
| Surface height bands overlap | 두 참조 면 또는 두 고정 범위가 같은 바닥을 중복 소유하는지. 참조 범위와 고정 범위의 겹침은 참조 면 우선으로 처리 |
| 노드는 정상인데 스폰 안 됨 | 함선 인지, Spawn Plan 실제 값, 포인트 권한, 스폰 위치의 다른 Pawn |
| 보스만 스폰 안 됨 | Encounter Enabled/Trigger와 스토리 게이트 |
| 계단에서 멈춤 | 노란 연결선, 충돌 소스와 높이 범위, 단차/폭/머리 공간. 필요하면 Cell Size를 낮춰 수동 재확인 |

캡슐을 크게 변경하면 Clearance도 함께 올린다. 통로를 억지로 통과시키기 위해 실제 캡슐보다 작게 지정하지 않는다. 높이 허용 범위 밖의 계단은 추출되지 않으므로 실제 통로에 맞게 조정한다. Seed를 추가하면 그 Seed가 속한 연결 영역만 유지하는 선택적 필터가 다시 활성화된다.

실행 중 충돌이나 기준 포인트를 변경했다면 서버 Rebuild 또는 PIE 재시작이 필요하다. 갑판이 기준 프레임과 따로 변형/애니메이션되는 경우는 지원하지 않는다. 동적 Pawn 간 군중 우회는 별도 기능이며 현재는 CharacterMovement 충돌과 정체 처리를 사용한다.

## 7. 검증 상태

- C++ ArtisticSW2026Editor Win64 Development 빌드 성공.
- BP_EnemyShip 컴파일·저장 성공. 보행면 2개, 기준 포인트 1, 스폰 플랜 저장값을 확인했다.
- 자동화 테스트는 요청대로 추가·실행하지 않았다.
- 사용자가 현재 구현의 정상 동작을 확인했다. 계단 이동과 지연/손실 환경 등 개별 시나리오의 검증 결과는 별도로 기록하지 않았다.
- BP 변경 전 파일은 Saved/DeckWalkBackups의 작업 시각별 폴더에 보관한다. 에셋 설정 결과는 Saved/codex_anchor_deck_height_final.log에서 확인할 수 있다.
