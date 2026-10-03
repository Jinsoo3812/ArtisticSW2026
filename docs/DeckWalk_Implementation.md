# EnemyShip 갑판 스폰 앵커와 보행면 AI 구현

> 기준: 2026-10-03 저장된 C++·Blueprint·Behavior Tree·DataTable. 에디터에서 값을 확인하고 플레이할 순서는 [에디터 안내](DeckWalk_Manual_Anchor_Editor_Setup.md)에, 보스 밸런스 테이블의 복구 내역은 [보스 데이터 진단](BossSpawn_Diagnosis_2026-10-03.md)에 둔다.

## 현재 구조

`UDeckWaypointComponent`는 소수의 **수동 스폰 앵커**로 사용한다. ID와 Transform, `WalkSurfaceId`, `CanSpawn`을 보관한다. 앵커의 로컬 Z는 면의 높이 기준으로도 사용하지만 XY가 보행면의 생성 반경을 정하지는 않는다. 일반 적과 보스의 이후 순찰·전투 목적지는 충돌 Mesh에서 만든 함선 로컬 보행면 그래프에서 선택한다.

| 담당 | 현재 책임 |
| --- | --- |
| `AEnemyShip` | 수동 앵커 등록, 보행면 생성과 적 풀 준비 순서 조정 |
| `UDeckEnemySpawnerComponent` | `SpawnPlan`, 기존 풀·정확한 ID의 스폰 예약과 점유, 소환용 앵커 선택 |
| `UBossEncounterComponent` | 조우 조건과 `BossSpawnPointId`를 통한 최초 보스 생성 |
| `FDeckSpawnAnchorValidator` | ID 중복·부착·면·스폰 참조·캡슐 여유·바닥의 읽기 전용 검증 |
| `FDeckWalkHeightResolver` | 앵커 위치를 `DeckMesh_Complex` 기준 로컬 높이 범위로 변환 |
| `FDeckWalkSurfaceSampler` / `FDeckWalkGraph` | 충돌 바닥, 경사, 공간 여유, 지지된 통로, 연결 영역과 경로 |
| `UDeckWalkAreaComponent` | 서버의 보행면 스냅샷과 위치·경로·예약 질의 |
| `UDeckWalkRouteComponent` | 적 한 명의 목표, 경로 진행·재탐색·정체 감지 |
| `UDeckEnemyNavigationComponent` / BT Task | 전투·순찰 목적지 선택과 이동 실행 |
| `UBossDeckPointSelector` / 보스 Ability | 보스 목적지 선택과 보행·돌진·은신 실행 |

## 저장된 함선 설정

`BP_EnemyShip`의 `DeckMesh_Complex` 아래에 앵커 세 개가 남아 있다. **ID 0도 유효**하며 이전 높이 기준이던 ID 1은 삭제됐다.

| 컴포넌트 | ID | 면 | 로컬 Z | 용도 |
| --- | ---: | --- | ---: | --- |
| `L_MeleeEnemySpawnPoint_1` | 0 | `LowerDeck` | 351 | 아래층 일반 적 스폰·높이 기준 |
| `U_MeleeEnemySpawnPoint_1` | 2001 | `UpperDeck` | 678 | 위층 일반 적 스폰·높이 기준 |
| `BossSpawnPoint` | 12345 | `UpperDeck` | 678 | 최초 보스 스폰 전용, `CanSpawn=false` |

두 면은 `WaypointReference` 높이 모드를 사용한다. `LowerDeck`은 ID 0을 기준으로 아래 31 cm·위 29 cm인 **320–380**, `UpperDeck`은 ID 2001을 기준으로 아래 3 cm·위 7 cm인 **675–685**다. 삭제된 Seed ID는 모두 제거했고 두 면의 `SeedPointIds`는 비어 있다. 빈 Seed 배열은 최소 크기를 만족하는 모든 유효 연결 영역을 유지한다.

일반 적 `SpawnPlan`에는 **아래층 `BP_DeckMeleeEnemy` 1명(ID 0), 위층 `T1_BP_DeckMeleeEnemy` 1명(ID 2001)**이 있다. 이전 원거리 슬롯은 사용자 결정으로 제거했다. 보스 최초 위치는 ID 12345를 계속 사용한다. Test_Level에서 보스가 설정된 함선의 클래스는 현재 `T2_BP_ShipBoss_Rogue`다. 다른 함선 인스턴스의 `BossClass=None`은 그대로 두었다.

## 보행면 생성과 스폰 승인

서버는 앵커를 등록한 뒤 보행면을 생성하고 풀을 준비한다. 생성기는 `DeckMesh_Complex`의 단위 Scale 좌표계에서 Floor Component의 충돌을 XY 격자로 샘플링한다. 면별 높이 범위, 경사, 캡슐 여유, 실제 바닥 지지와 인접 통로를 검사한다. `WalkingConnections`를 설정해도 통로의 물리적 지지가 없으면 층 사이를 연결하지 않는다. `Rebuild`는 Revision을 올리므로 이전 위치 핸들과 경로는 다시 사용할 수 없다.

`FDeckWalkLocation`은 `NodeIndex`, `SurfaceId`, `LocalFloor`, `Revision`을 가진다. `ResolveWaypoint`는 스폰 앵커를 지정 면의 실제 바닥으로 투영하고, `ResolveSpawnTransform`은 바닥에서 배의 Up 방향으로 캐릭터 캡슐 반높이와 2 cm를 더해 생성 위치를 구한다. 캐릭터의 현재 층은 앵커 ID가 아니라 `ResolveActorOnDeck`의 실제 발밑으로 판정한다.

보행면 생성, 앵커 투영 또는 필요한 캡슐·충돌 검사가 실패하면 일반 적과 보스의 생성·활성화를 차단하고 서버 로그에 이유를 남긴다. `SpawnPlan`은 각 슬롯의 정확한 앵커 ID를 사용하며 다른 ID로 바꾸지 않는다. 보스 소환 후보도 기존 일반 적 스폰 앵커 중 보스 위치에서 도달 가능한 지점으로 제한한다. 기존 풀·스폰 예약·재시도 계약은 유지한다.

## 순찰과 일반 전투

`UDeckWalkRouteComponent`가 `SetPatrolGoal`, `SetActorGoal`, `SetLocationGoal`, `TickRoute`로 경로를 관리한다. 순찰은 현재 연결 영역의 보행 노드를 고르고, 근접 적은 대상의 실제 바닥을 추적한다. 원거리 전투는 도달 가능한 보행 노드의 사거리·선호 거리·실제 시야·캡슐 여유·경로 비용을 평가한다. 시야가 막힌 사격 위치를 옮길 때도 보행면 후보를 사용한다.

`UDeckWalkAreaComponent`는 서버에서 최종 전투 위치를 캡슐 크기에 따라 예약하고, 이동 취소·사망·풀 복귀 때 해제한다. 일반 갑판 적의 공격 판정은 자신과 대상의 발밑 보행면을 찾고 같은 Surface인지 확인한 다음 기존 거리와 시야 조건을 적용한다. 실제 이동 입력은 매 순간 함선 로컬 방향을 월드 방향으로 바꾸어 CharacterMovement에 전달하고, Movement Base는 현재 발밑 충돌 컴포넌트를 따른다.

## 보스 이동과 능력

보스의 **최초 스폰 ID**와 이후 **이동 목적지**를 분리했다. `BB_RogueBoss`의 `DestinationLocation`은 함선 로컬 바닥 위치를 보여주는 Vector이고, 보스가 실제 유효성을 확인하는 목적지는 `FDeckWalkLocation` 핸들이다. 보스 BT의 선택·능력 Task가 이 키를 사용한다.

- **Walk/Strafe:** 현재 바닥의 도달 가능한 후보와 앞쪽 지지 바닥을 사용한다.
- **DashSlash:** 같은 Surface의 연속 지지 구간과 종점 여유를 검사한다. 선택한 시작·끝 로컬 경로 하나를 예고, 이동, 피해 판정에 사용한다. 경로가 무효가 되거나 시작점이 달라지면 취소한다.
- **Vanish:** 기존 앞/뒤 관계를 평가하면서 **보스가 서 있는 층**에서만 목적지를 고른다. 재등장 전에 목적지 Revision과 점유를 다시 확인한다.
- **Summon:** `BP_DeckMeleeEnemy`의 기존 비활성 풀 액터를 재사용한다. 새 전용 풀이나 스폰 앵커를 만들지 않았다. 위층 보스가 아래층 앵커에 도달할 수 없는 현재 설정에서는 위층 일반 적 앵커가 비어 있어야 한다.

Player 기준으로 재등장 직후 바로 공격할 수 있는 위치를 선택하는 조건은 후속 작업이다.

## 제거된 시스템과 유지한 에셋 이름

자동 Waypoint 생성·삭제·링크, 이동용 Waypoint 플래그와 대기 시간, `UDeckNavigationComponent`의 링크 그래프, 전투 목적지 정수 ID, 고정 바닥 스폰 우회 경로를 제거했다. 소수의 수동 앵커를 검사하는 `ValidateDeckWaypoints`는 유지했다. 기존 BT 에셋의 클래스 참조를 보존하기 위해 `BTT_SelectDeckWaypoint`, `BTT_MoveToDeckWaypoint`, `BTT_WaitAtDeckWaypoint`, `DeckWaypointMovementInterface`의 **타입 이름**은 그대로 두었지만 이동과 대기는 보행면을 사용한다.

## 현재 범위와 확인 결과

서버가 보행면과 목적지 선택·예약·이동을 처리하고, 클라이언트는 CharacterMovement와 보스 능력 연출의 복제 결과를 사용한다. 함선의 월드 이동만으로 로컬 그래프를 다시 생성하지 않는다.

아래층 높이를 320–380으로 넓힌 후 저장된 BP와 Test_Level의 두 함선에서 `LowerDeck` **302개**, `UpperDeck` **387개** 노드와 `Ready=1`을 확인했다. 보스 함선은 수동 앵커 3개에 대한 바닥·캡슐 검증이 **오류 0개**였다. 에디터용 C++ 빌드는 통과했다. 자동화 테스트와 실제 PIE/서버·클라이언트 플레이 검증은 실행하지 않았다.

현재 두 높이 범위 사이의 계단 중간 높이는 보행면에 포함되지 않아 층간 보행 경로가 자동으로 생기지 않는다. 연결을 구현할 때는 계단의 실제 충돌 바닥과 중간 높이를 포함하는 면 구성을 별도로 정해야 한다.

Unreal의 [Behavior Tree](https://dev.epicgames.com/documentation/en-us/unreal-engine/behavior-tree-in-unreal-engine---overview), [EQS](https://dev.epicgames.com/documentation/en-us/unreal-engine/environment-query-system-in-unreal-engine), [Navigation System](https://dev.epicgames.com/documentation/unreal-engine/navigation-system-in-unreal-engine?lang=en-US) 설명은 역할 분리의 참고 자료다. 현재 함선 이동은 배 로컬의 자체 그래프를 사용한다.
