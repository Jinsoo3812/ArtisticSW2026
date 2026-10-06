# 수동 스폰 앵커·보행면 이전 후 에디터 확인

작성: 2026-10-03. 자동화 테스트는 실행하지 않았다. 아래 절차로 서버 PIE에서 이동·능력·복제를 확인한다.

## 1. 앵커와 보행면 확인

에디터를 다시 열고 `BP_EnemyShip`을 연다. `DeckMesh_Complex` 하위의 기본 앵커 ID는 아래층 0·1, 위층 10·11·12다.

| 컴포넌트 | ID | Walk Surface Id | Can Spawn | DeckMesh_Complex 로컬 Z |
| --- | ---: | --- | --- | ---: |
| L_MeleeEnemySpawnPoint_1 | 0 | LowerDeck | 켬 | 351 |
| L_EnemySpawnPoint_1 | 1 | LowerDeck | 끔 | 351 |
| U_MeleeEnemySpawnPoint_1 | 10 | UpperDeck | 켬 | 678 |
| U_EnemySpawnPoint_11 | 11 | UpperDeck | 끔 | 678 |
| BossSpawnPoint | 12 | UpperDeck | 끔 | 678 |

새 ID 1·11 앵커는 각각 기존 0·10 앵커 위치에서 시작한다. 별도 위치로 옮기고 바닥·캡슐 검증을 마친 뒤 일반 적 스폰에 쓸 때만 Can Spawn을 켠다. 보스 전용 ID 12는 Can Spawn을 끈 상태에서도 Boss Spawn Point Id로 사용할 수 있다.

기존 `T1_BP_EnemyShip`~`T4_BP_EnemyShip`은 예전 대량 Waypoint 구성을 가진 별도 에셋이다. 이번 기본 ID 설정은 `BP_EnemyShip`과 `Test_Level`의 해당 배 인스턴스에 적용했다. 티어별 배를 만들 때는 갱신된 `BP_EnemyShip`을 복제한 뒤 각 티어 설정을 옮긴다.

`DeckWalkAreaComponent → Surfaces`에서 확인한다.

| 항목 | LowerDeck | UpperDeck |
| --- | --- | --- |
| Height Mode | Waypoint Reference | Waypoint Reference |
| Height Reference Point Id | 0 | 10 |
| Height Below Reference | 31 | 3 |
| Height Above Reference | 29 | 7 |
| 계산된 로컬 높이 범위 | 320–380 | 675–685 |
| Seed Point Ids | 비움 | 비움 |
| Required | 켬 | 켬 |

Floor Component Names와 Obstacle Component Names는 기존 `DeckMesh_Simple`, `DeckMesh_Complex`를 사용한다. Trace Complex는 기존처럼 꺼져 있다. 프레임인 DeckMesh_Complex의 월드 Scale은 1이어야 한다. 높이 기준은 앵커 Z이며 앵커 XY는 보행면의 생성 반경을 정하지 않는다. 기준 앵커를 위아래로 옮기면 높이 범위도 함께 이동한다.

아래층 범위는 사용자 요청으로 기존 345–355에서 320–380으로 넓혔다. 기준 앵커의 로컬 Z는 351로 유지하고 아래 31·위 29를 적용했다.

이 설정에서 BP와 Test_Level의 두 함선 모두 **LowerDeck 302개 / UpperDeck 387개 / Ready=1**을 확인했다. 이 숫자는 현재 충돌 Mesh와 샘플 간격 기준이며 설정을 바꾸면 달라질 수 있다.

## 2. 일반 적 설정 확인

`DeckEnemySpawnerComponent → Enable Spawning`을 켜고 Spawn Plan이 두 항목인지 확인한다. 이미 에셋에 적용되어 있다.

| 슬롯 | Enemy Class | Stats Row | Spawn Point Id |
| --- | --- | --- | ---: |
| 0 | BP_DeckMeleeEnemy | DT_EnemyBaseStat / T1_Deck_Melee | 0 |
| 1 | T1_BP_DeckMeleeEnemy | DT_EnemyBaseStat / T1_Deck_Melee | 10 |

원거리 슬롯은 제거했다. `Compile → Save`한 뒤 Test_Level에서 함선을 선택해 `Validate Deck Waypoints`를 실행한다. 일반 적만 사용하는 함선에 Boss Class가 없다면 Boss Encounter의 Encounter Enabled를 끈 상태에서 검증한다.

PIE에서 기존 시야 배치 조건을 충족시킨다. 아래층·위층에 근접 적이 한 마리씩 나타나고, 각 층에서 스폰 앵커 이외의 여러 위치를 순찰하는지 확인한다. 플레이어를 같은 층에 올려 추적·공격·공격 거리에서 정지를 확인한다.

## 3. 보스 설정 확인

Test_Level에서 **보스를 사용할 함선 인스턴스**를 선택하고 `BossEncounterComponent`를 확인한다.

1. Encounter Enabled를 켠다.
2. Boss Class에 원하는 보스 클래스를 지정한다. 현재 저장된 보스 함선은 `T2_BP_ShipBoss_Rogue`를 사용한다. T1을 시험하려면 `T1_BP_ShipBoss`로 바꾼다.
3. Boss Spawn Point Id는 **12**다.
4. 현재 Encounter Trigger는 **Player Ship Sight**, Required Story Node는 **Story.GameStarted**, Stop After Story Node는 **Story.MiddleBoss1Defeated**다. 해당 스토리 상태에서 플레이어 함선을 인식시키면 최초 등장한다.
5. 보스가 설정된 함선에서 Validate Deck Waypoints를 실행한 뒤 PIE로 최초 등장을 확인한다.

보스의 밸런스 테이블 구조와 BP별 행 선택은 [보스 조우 데이터 복구 기록](BossSpawn_Diagnosis_2026-10-03.md)에 정리했다. 현재 Test_Level의 T2 보스는 조우 밸런스 행이 지정되지 않은 상태다.

## 4. 보스 소환 확인

보스들의 Summoned Enemy Class는 **BP_DeckMeleeEnemy**다. 새 풀을 추가하지 않고 기존 풀의 비활성 기본 근접 적을 재사용한다.

1. 위층의 T1 근접 적은 살아 있도록 둔다.
2. 아래층의 기본 근접 적을 처치하고 사망 연출·풀 복귀가 끝날 때까지 기다린다.
3. 보스의 기존 소환 조건과 BT 흐름을 충족시킨다. T1 밸런스 사용 시 보스 체력 50% 이하 진입이 소환 요청 조건이다.
4. 위층 근접 적이 순찰해 ID 10 앵커를 비운 상태에서 확인한다. 현재 두 층은 보행 경로로 연결되지 않으므로 위층 보스의 소환 후보는 도달 가능한 위층 앵커다. 플레이어와 보스에 대한 기존 최소 거리 및 바닥·충돌 검증을 만족하면 비활성 기본 근접 적이 재사용되는지 확인한다.

일반 적 둘이 모두 활성 상태라면 사용 가능한 기본 근접 적이 없다. T1 근접 적은 다른 정확한 클래스이므로 기본 근접 적을 대신하지 않는다. 현재 편성에서 한 번에 재사용 가능한 기본 근접 적은 최대 한 마리다. 기존의 함선 승무원 전멸 처리도 유지되므로 소환 확인 중 일반 적 둘을 모두 처치하지 않는다.

## 5. 보스 이동·DashSlash·은신 확인

`BB_RogueBoss`의 목적지 키는 **DestinationLocation / Vector**다. 기존 보스 BT의 목적지 선택·능력 Task에도 적용했다. 이 Vector는 함선 로컬 바닥 위치 표시용이며, 실제 유효성은 보스가 보관하는 Surface Id·Node Index·Revision 핸들로 검사한다.

1. 플레이어와 보스를 같은 층에 둔다. 보스 보행과 좌우 이동이 보행면 안에서 이루어지는지 확인한다.
2. DashSlash의 예고 선과 실제 이동이 일치하고, 난간·구멍·다른 층을 통과하는 직선 목적지가 선택되지 않는지 확인한다.
3. 은신은 **보스가 서 있던 층에서만** 재등장하는지 확인한다. 기존 앞/뒤 관계 설정은 유지한다.
4. 이동·능력 중단, 사망, 보행면 Rebuild 후 목적지 예약과 이동 잠금이 정리되는지 확인한다.
5. 함선 이동·회전·흔들림 중에도 적과 목적지가 함선을 따라가는지 확인한다. 마지막으로 Listen Server와 클라이언트 화면을 비교한다.

**후속 작업:** Player를 기준으로 재등장 직후 공격할 수 있는 위치를 선택하는 조건은 이번 구현에 포함하지 않았다. 공격 거리·방향·LOS·플레이어의 층 처리 규칙을 정한 뒤 은신 후보 선택에 추가한다.

현재 두 높이 범위 사이의 계단 중간 높이는 샘플링 대상에 포함되지 않는다. Walking Connections는 실제 연속 바닥과 단차 조건을 만족할 때만 연결되므로 두 층 사이 경로가 자동으로 생기지는 않는다. 계단 보행이 필요하면 별도 보행면 범위를 먼저 정해야 한다.

## 6. 실패 원인 확인

`DeckWalkAreaComponent → Draw Debug Area`를 켜고 Debug Surface Filter를 LowerDeck 또는 UpperDeck으로 설정하면 층별로 볼 수 있다. 서버 Output Log에서 `[DeckWalk]`, `[DeckEnemySpawner]`, `[BossEncounter]`, `[DeckSpawnAnchorValidation]`을 검색한다.

- `ReferencePoint=0`, `SamplingZ=[320,380]` 및 `ReferencePoint=10`, `SamplingZ=[675,685]`, 각 `Nodes>0`, `Ready=1`이 정상 기준이다.
- `WalkAreaNotReady`: 필수 보행면 생성 실패. 높이 기준 ID·면 이름·충돌 Mesh·프레임 Scale을 확인한다.
- `NoWalkableSpawnFloor`: 앵커의 지정 면에 사용할 바닥이 없다. 위치·Walk Surface Id·캡슐 여유를 확인한다.
- `SpawnCapsuleBlockedNow`: 다른 액터가 현재 생성 위치를 막고 있다.
- `MissingOrAbstractBossClass`: 검증한 함선의 Boss Class가 비어 있거나 생성 불가능한 클래스다.

보행면이나 스폰 바닥을 찾지 못하면 생성을 차단한다. 고정 Z나 다른 앵커로 대체하는 경로는 제거했다.
