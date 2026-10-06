# Lvl_CY EnemyShip 배치와 에디터 설정 정리

확인일: 2026-10-07. 현재 작업 폴더에 저장된 Lvl_CY를 Unreal Engine 5.7로 직접 불러와 51척의 EnemyShip, 클래스 기본값, 컴포넌트, 아키타입, 스킬 모듈, 참조 DataTable을 조사했다. 에셋과 레벨을 저장하거나 설정을 변경하지 않았다. 플레이 실행 중 수치나 저장되지 않은 다른 에디터의 변경 내용은 조사 범위에 포함하지 않는다.

## 1. 기준 레벨과 설정의 우선순위

- 에디터 시작 레벨: `/Game/Level/Lvl_CY`.
- 게임 시작 레벨: `/Game/Level/ConnectionLobby`.
- 서버 기본 레벨: `/Game/Level/Lvl_CY`.
- Lvl_CY의 51척은 전부 `/Game/Blueprints/Ship/Enemy_Ship/Blueprints/BP_EnemyShip`의 인스턴스다. 티어별 별도 함선 BP를 사용하지 않는다.

이 문서에서 **기본값**은 C++에서 선언한 값과 현재 BP가 저장한 값을 구분한다. 레벨 인스턴스의 수정값은 BP 기본값보다 우선한다. 다만 적 함선 초기화가 아키타입의 스탯과 항해 프로필을 다시 적용하므로, 런타임의 최종 설정 소스를 확인해야 한다.

| 내용 | 실제 설정 소스 | 수정 위치 |
| --- | --- | --- |
| 체력·포격 피해·재장전·포탄 속도·추진/선회 배율 | EnemyShipArchetype.SpecRow | 아키타입 → Spec Row → DT 행 |
| 탐지·선회 거리·복귀 규칙 | 아키타입의 Navigation Profile | 아키타입 에셋 |
| 편대 구성 | 레벨 액터의 Squad ID | World Outliner → 함선 → Details |
| 함선 스킬 구성·우선순위 | 아키타입 Skill Modules와 모듈 에셋 | Content Browser → Data |
| 스킬 피해·쿨다운·생성 클래스·연출 | 모듈의 Ability Class BP | 해당 BP_GA의 Class Defaults |
| 갑판 적 종류·수량·티어·위치 | DeckEnemySpawnerComponent.SpawnPlan | 함선 BP의 해당 컴포넌트 |
| 사람 보스 등장 | BossEncounterComponent | 레벨 액터의 해당 컴포넌트 |
| 상자 보상 구역 | 함선 액터의 Chest 설정 | 레벨 액터 Details → Chest |

**EnemyShip의 상속된 Ship Stat Row는 사용하지 않는다.** 현재 51척 모두 그 필드는 비어 있지만, 아키타입 Spec Row가 정상 연결되어 있다. 그 빈 필드를 채우는 것은 이 함선의 스탯 수정 방법이 아니다.

## 2. 현재 함선 종류와 실제 스탯

현재 사용 중인 스탯 테이블은 `/Game/Blueprints/Item/Data/ShipUpgrade/DT_ShipStat`이다. 과거 생성 스크립트에 나오는 `/Game/Blueprints/Ship/Data/DT_ShipStat` 경로와 구분한다.

| Archetype | 척수 | Spec Row | HP | 포격 피해 | 재장전 초 | 포탄 cm/s | 추진 배율 | 선회 배율 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| DA_ES_Normal_1 | 12 | EnemyShip_Normal_1 | 100 | 10 | 5 | 4250 | 3 | 2 |
| DA_ES_Normal_2 | 9 | EnemyShip_Normal_2 | 150 | 15 | 4 | 4900 | 4 | 3 |
| DA_ES_Normal_3 | 9 | EnemyShip_Normal_3 | 200 | 20 | 4 | 5500 | 5 | 4 |
| DA_ES_Normal_4 | 18 | EnemyShip_Normal_4 | 250 | 25 | 3 | 6000 | 6 | 5 |
| DA_ES_Charge | 1 | EnemyShip_Mid_1 | 300 | 10 | 4 | 4250 | 4 | 1 |
| DA_ES_Torpedo_Obstacle | 1 | EnemyShip_Mid_3 | 600 | 20 | 2.5 | 5500 | 5 | 3 |
| DA_ES_TimeStop | 1 | EnemyShip_Final | 1000 | 25 | 2.5 | 6000 | 6 | 4 |

Normal 아키타입은 `Data/Archetype/Normal`, 특수 아키타입은 `Data/Archetype/Elite`에 있다. 함선 아키타입을 바꾸면 함선 스탯과 스킬이 바뀐다. 갑판 적의 Stats Row와 보스 Class는 별도다.

모든 아키타입의 공통 저장값: Detection Distance 30,000cm, Orbit Tolerance 5,000cm, Return Trigger Distance 100,000cm, Lost Target Return Delay 10초, Orbit Distance Spacing 3,000cm, Zero Health Cannon Cooldown Multiplier 3, Cannon Lead Speed -1, Selection Policy Highest Priority.

| Archetype | 기본 선회 거리 cm | 복귀 도착 거리 cm | 복귀 추진 배율 | 저장된 Trackable Target Speed cm/s | 저장된 Projectile Flight Time 초 |
| --- | --- | --- | --- | --- | --- |
| DA_ES_Normal_1 | 10000.0 | 2000.0 | 3.0 | 200.0 | 3.0 |
| DA_ES_Normal_2 | 13333.0 | 2000.0 | 3.0 | 500.0 | 3.0 |
| DA_ES_Normal_3 | 16666.0 | 2000.0 | 3.0 | 600.0 | 3.0 |
| DA_ES_Normal_4 | 20000.0 | 2000.0 | 3.0 | 800.0 | 3.0 |
| DA_ES_Charge | 20000.0 | 800.0 | 3.0 | 1000.0 | 3.0 |
| DA_ES_Torpedo_Obstacle | 20000.0 | 800.0 | 3.0 | 1000.0 | 3.0 |
| DA_ES_TimeStop | 20000.0 | 800.0 | 1.0 | 1000.0 | 3.0 |

현재 일반 포격 경로는 **DT의 Cannonball Speed, Cannon Lead Speed, 포격 모듈의 타원 분산 설정**으로 탄도를 계산한다. Cannon Aim Profile의 Trackable Target Speed/Projectile Flight Time은 저장되어 있지만, 현재 일반 포격의 BuildShotSolution에서는 읽지 않는다. 이 프로필은 장애물 발사 탄도에 사용된다. 따라서 Trackable Target Speed만 바꿔 일반 포격의 명중률이 바뀐다고 기대하면 안 된다.

## 3. 편대별 배치 목록

아래 표는 51척 전부를 포함한다. 같은 행의 함선은 아키타입·편대·보상 구역이 같다. 보스 조우는 전부 꺼져 있고 갑판 Spawn Plan은 전부 동일하다. 위치·회전과 저장 식별자는 아래 개별 목록에서 확인할 수 있다.

| Squad ID | 함선 이름 | Archetype | 보상 구역 |
| --- | --- | --- | --- |
| Final | BP_ES_Normal_4_16, BP_ES_Normal_4_17, BP_ES_Normal_4_18 | DA_ES_Normal_4 | FINAL |
| Final | BP_ES_Final | DA_ES_TimeStop | FINAL |
| Normal_1_1 | BP_ES_Normal_1_1, BP_ES_Normal_1_2, BP_ES_Normal_1_3 | DA_ES_Normal_1 | MID1 |
| Normal_1_2 | BP_ES_Normal_1_4, BP_ES_Normal_1_5, BP_ES_Normal_1_6 | DA_ES_Normal_1 | MID1 |
| Normal_1_3 | BP_ES_Normal_1_10, BP_ES_Normal_1_11, BP_ES_Normal_1_12 | DA_ES_Normal_1 | MID1 |
| Normal_1_4 | BP_ES_Normal_1_7, BP_ES_Normal_1_8, BP_ES_Normal_1_9 | DA_ES_Normal_1 | MID1 |
| Normal_1_4 | BP_ES_Mid_1 | DA_ES_Charge | MID1 |
| Normal_2_1 | BP_ES_Normal_2_1, BP_ES_Normal_2_2, BP_ES_Normal_2_3 | DA_ES_Normal_2 | MID2 |
| Normal_2_2 | BP_ES_Normal_2_4, BP_ES_Normal_2_5, BP_ES_Normal_2_6 | DA_ES_Normal_2 | MID2 |
| Normal_2_3 | BP_ES_Normal_2_7, BP_ES_Normal_2_8, BP_ES_Normal_2_9 | DA_ES_Normal_2 | MID2 |
| Normal_3_1 | BP_ES_Normal_3_1, BP_ES_Normal_3_2, BP_ES_Normal_3_3 | DA_ES_Normal_3 | MID3 |
| Normal_3_2 | BP_ES_Normal_3_4, BP_ES_Normal_3_5, BP_ES_Normal_3_6 | DA_ES_Normal_3 | MID3 |
| Normal_3_3 | BP_ES_Normal_3_7, BP_ES_Normal_3_8, BP_ES_Normal_3_9 | DA_ES_Normal_3 | MID3 |
| Normal_3_3 | BP_ES_Normal_3_10 | DA_ES_Torpedo_Obstacle | MID3 |
| Normal_4_1 | BP_ES_Normal_4_1, BP_ES_Normal_4_2, BP_ES_Normal_4_3, BP_ES_Normal_4_7, BP_ES_Normal_4_8, BP_ES_Normal_4_9 | DA_ES_Normal_4 | FINAL |
| Normal_4_2 | BP_ES_Normal_4_4, BP_ES_Normal_4_5, BP_ES_Normal_4_6 | DA_ES_Normal_4 | FINAL |
| Normal_4_3 | BP_ES_Normal_4_10, BP_ES_Normal_4_11, BP_ES_Normal_4_12 | DA_ES_Normal_4 | FINAL |
| Normal_4_4 | BP_ES_Normal_4_13, BP_ES_Normal_4_14, BP_ES_Normal_4_15 | DA_ES_Normal_4 | FINAL |

관찰 사항:

- `BP_ES_Normal_3_10`은 이름과 달리 일반 3단계가 아니다. `DA_ES_Torpedo_Obstacle / EnemyShip_Mid_3`를 사용하는 HP 600의 특수 함선이다.
- `Normal_1_4`에는 일반 1단계 3척과 Charge 함선 `BP_ES_Mid_1`이 함께 있다.
- `Normal_4_1`에는 1·2·3번과 7·8·9번, 총 6척이 들어 있다. 두 개의 별도 3척 편대를 원한다면 Squad ID를 직접 분리해야 한다.
- `Final`은 4단계 일반 함선 16·17·18번과 `BP_ES_Final`, 총 4척이다.
- `EnemyShip_Mid_2` 행은 DT에 있지만 이 레벨 EnemyShip의 Spec Row에서 사용하지 않는다. 이것만으로 다른 액터로 구성된 중간보스2 콘텐츠의 유무를 판단하지 않는다.

같은 Squad ID는 단순한 표시용 이름이 아니다. 편대의 평균 Ideal Distance를 중심으로 평균 Orbit Distance Spacing 간격을 두어 각 함선의 선회 거리를 다시 배정한다. 실제 정렬은 Outliner Label이 아닌 액터 내부 FName이다. 복귀·비활성화·사망 상태에 따라 편대 구성원이 바뀌면 거리가 재계산될 수 있다.

예: 동일 Normal_1 3척 편대가 모두 참여하면 7,000 / 10,000 / 13,000cm를 배정한다. 편대원의 아키타입을 서로 다르게 설정하면 평균값이 달라진다.

## 4. 레벨 액터에서 별도로 설정할 항목

새 BP_EnemyShip을 배치하거나 기존 함선의 역할을 바꿀 때 다음을 설정한다. 현재 51척에는 대부분 이미 지정되어 있다.

| Details 위치 | 직접 설정할 내용 | 현재 상태 / 의미 |
| --- | --- | --- |
| Transform | 수면 위치, 회전, 간격 | 초기 배치 위치가 복귀 지점이 된다. 별도 Return Point 액터를 지정하는 필드는 없다 |
| Ship → AI → Data | Enemy Ship Archetype | BP 기본은 DA_ES_Normal_1. 2~4단계·특수함은 인스턴스에서 변경 |
| Ship → AI | Squad ID | BP 기본 Squad_Alpha를 각 편대 ID로 변경. Final은 별도 스토리 의미가 있음 |
| Chest → Progression | Progression Zone | Mid1 / Mid2 / Mid3 / Final을 레벨 구역에 맞게 지정 |
| Chest → Progression | Progression Kind | 현재 전부 Ship Guarded. 특수 함선 이름이나 HP로 다른 상자 종류가 자동 선택되지는 않음 |
| Chest → Data Driven | Spawn Mode | 현재 전부 Guarded. 다른 보상 흐름을 원할 때 명시 변경 |
| Chest → Spawn | Chest Class Override | 현재 전부 BP_Storage_Chest. BP 액터 기본은 None이므로 같은 결과를 원하면 지정 |
| Loot → Spawn | Enabled, Point Weight | 현재 true / 1. 배치 참여 여부와 선택 가중치 |
| Loot → Placement | Align Bottom, Ground Clearance, Trace 거리 | 현재 true / 1 / 위200 / 아래1000cm. 상자 위치를 옮겼을 때 바닥 정렬 확인 |
| BossEncounterComponent | Encounter Enabled | 현재 51척 false. **BP 기본은 true이고 Boss Class는 None**. 새 일반 함선도 false로 명시 설정 |

다음은 역할에 따라 조정하는 선택 항목이며 새 함선마다 반드시 바꿔야 하는 항목은 아니다.

| 위치 | 항목 | 현재 51척 공통값 |
| --- | --- | --- |
| Ship → Optimization | Enable Distance Optimization / Distance Optimization Range | true / 100,000cm(1km) |
| Ship → AI → Cannon Lead | Override Cannon Lead Speed / Cannon Lead Speed Override | false / 저장값 1,000cm/s. override가 켜질 때만 적용 |
| Ship → Crew | Crew Defeated Damage Multiplier | 3. 승무원 전멸 뒤 선체 피격 피해 배율 |
| Ship → Death | Destroy After Death Delay | 5초 |
| UI → HealthBar | Offset / Draw Size | 현재 BP/레벨 (0,0,2000) / (220,28). C++ 선언 기본 Offset Z는 300 |
| DeckEnemySpawnerComponent → Timing | Spawn Start Delay | 3초. 시작 즉시 적을 생성한다는 의미가 아니라 최소 배치 대기시간 |
| DeckWalkAreaComponent → Spawn | Spawn Height Offset | 30cm. 기존 수동 가이드에 나온 90cm와 현재 값이 다름 |
| CabinWaterCullComponent | Water Cull Enabled / Activation Distance | true / 5,000cm(50m) |

함선의 Chest 설정은 모든 소유 ChestSpawnPoint Child Actor에 전달된다. 코드에서 Environment를 ShipDeck으로, OwningShip을 자기 함선으로 설정한다. Guarded 상자의 Guard Characters는 등록된 승무원으로 구성되고, 이후 생성되는 갑판 적도 자동 등록된다. 따라서 이 자동 연결을 레벨에서 매번 수동 입력할 필요는 없다. 함선 액터의 Guard Characters 배열을 임의 입력해 자동 승무원 등록을 대체하는 방식도 적합하지 않다.

현재 51척은 Enemy 태그와 서로 다른 SWRoomStableId 태그를 갖고 있다. Enemy 태그와 저장용 ID는 기존 시스템이 관리한다. 스냅샷 컴포넌트는 에디터 복제 시 새 저장 ID를 생성하므로 수동으로 같은 ID를 붙이지 않는다.

## 5. 갑판 적: BP에서 따로 구성해야 하는 내용

`BP_EnemyShip → Components → DeckEnemySpawnerComponent`에서 설정한다. Enable Spawning과 Spawn Plan은 **EditDefaultsOnly**다. 현재 구조에서는 레벨 인스턴스 Details에서 함선별 Spawn Plan을 직접 바꾸는 용도가 아니다. 지역마다 편성을 다르게 하려면 갱신된 BP를 기준으로 자식/복제 BP를 만들고 해당 컴포넌트 기본값을 구성한다.

현재 51척의 편성은 다음과 같다.

| 순서 | Enemy Class | Stats Row | Spawn Point ID |
| --- | --- | --- | --- |
| 0 | BP_DeckMeleeEnemy | DT_EnemyBaseStat / T1_Deck_Melee | 0 |
| 1 | T1_BP_DeckMeleeEnemy | DT_EnemyBaseStat / T1_Deck_Melee | 10 |

각 적의 기본 HP는 50, Strength는 10, 이동/공격 속도 배율은 1이다. 무기·상태 효과로 최종 수치가 바뀔 수 있다. **함선이 Normal_4 또는 Final이어도 이 갑판 적 두 마리는 T1이다.** 현재 원거리 적 슬롯은 없다.

직접 설정할 항목:

- Enable Spawning: 갑판 적 사용 여부.
- Spawn Plan 배열 길이: 항목 하나가 적 한 명이다. Count를 따로 입력하지 않는다. 최대 32개이며 슬롯 간 Spawn Point Id를 중복시키지 않는다.
- 각 슬롯의 Enemy Class: 생성 가능한 ADeckEnemy 파생 BP.
- 각 슬롯의 Stats Row: 필요한 단계의 DT + Row Name. 지정한 행은 해당 적 BP의 Default Stats Row보다 우선한다.
- 각 슬롯의 Spawn Point Id: 같은 함선에 존재하는 고유 Waypoint ID. Can Spawn이 켜진 앵커를 사용한다.
- Timing: Spawn Start Delay 3, Sight Activation Delay 0.25, Activation Interval 0.35초.
- Retry: Max Spawn Retries 3, Spawn Retry Interval 0.5초.
- Random Seed: 1337.

플레이어 함선 인식뿐 아니라 함선의 항해 상태가 Approach/Orbit로 들어갈 때도 일반 갑판 배치를 요청한다. 최소 시작 지연과 배치 지연이 적용된다. BossEncounter의 Player Ship Sight 트리거와 일반 적 배치는 같은 조건으로 단정하지 않는다.

보스 소환을 쓰려면 보스의 Summoned Enemy Class와 풀에 준비된 Spawn Plan Enemy Class가 정확히 일치해야 한다. 기본 BP와 T1 파생 BP는 별도 클래스다. 함선/보스 티어 숫자나 Encounter DT 수량만 바꿔서는 풀의 편성이 자동 생성되지 않는다.

## 6. 스폰 앵커와 보행면

현재 51척은 아래 6개 앵커를 동일하게 갖고 있으며 전부 DeckMesh_Complex 하위에 부착되어 있다. 위치는 DeckMesh_Complex 기준 로컬 cm다.

| 컴포넌트 | Waypoint ID | Surface | Can Spawn | 로컬 X | 로컬 Y | 로컬 Z |
| --- | --- | --- | --- | --- | --- | --- |
| L_MeleeEnemySpawnPoint_0 | 0 | LowerDeck | True | 70.0 | -0.0 | 341.0 |
| L_MeleeEnemySpawnPoint_1 | 1 | LowerDeck | True | 680.39 | -0.0 | 351.0 |
| U_MeleeEnemySpawnPoint_0 | 10 | UpperDeck | True | 0.0 | 0.0 | 678.0 |
| U_RangedEnemySpawnPoint_1 | 11 | UpperDeck | True | 0.0 | 310.0 | 678.0 |
| BossSpawnPoint | 12 | UpperDeck | False | -1096.5 | -35.82 | 678.0 |
| U_RangedEnemySpawnPoint_2 | 12 | UpperDeck | True | -0.0 | -330.0 | 678.0 |

**현재 오류: BossSpawnPoint와 U_RangedEnemySpawnPoint_2의 ID가 둘 다 12다.** 51척 모두 같은 구성이다. 에디터의 Validate Deck Waypoints를 대표 일반함·중간함·최종함에서 실행했고 모두 DuplicatePointId 1개를 보고했다. 현재 일반 Spawn Plan은 0·10만 사용하므로 이 오류만으로 일반 적 두 마리의 생성 실패를 단정하지 않는다. 다만 ID 12를 사용하는 보스/추가 스폰을 구성하기 전에 해결해야 한다.

수정안: BossSpawnPoint는 12를 유지하고 U_RangedEnemySpawnPoint_2에 사용하지 않는 ID(예: 13)를 지정한 뒤, 그 앵커를 참조하는 Spawn Plan과 다른 설정도 같이 맞춘다. 이 문서 작성 과정에서 실제 값을 변경하지 않았다.

새 앵커/배 모델을 구성할 때 직접 설정할 것:

- 함선 안에서 고유한 Waypoint Id.
- 정확한 Walk Surface Id: 현재 LowerDeck 또는 UpperDeck.
- 일반 적 스폰용 Can Spawn. 보스 전용 앵커는 false여도 Boss Spawn Point Id로 사용 가능하다.
- 바닥 위 로컬 위치·방향, 다른 앵커/적/난간과의 캡슐 여유.
- DeckMesh_Complex 하위 부착. 월드에 따로 둔 포인트로 대체하지 않는다.

보행면 설정은 `BP_EnemyShip → DeckWalkAreaComponent`에서 지정한다.

| 항목 | LowerDeck | UpperDeck |
| --- | --- | --- |
| Height Mode | Waypoint Reference | Waypoint Reference |
| Height Reference Point ID | 0 | 10 |
| 기준 앵커 현재 Z | 341 | 678 |
| Height Below / Above Reference | 31 / 29 | 3 / 7 |
| 계산되는 샘플링 Z 범위 | 310–370 | 675–685 |
| Floor Component Names | DeckMesh_Simple, DeckMesh_Complex | DeckMesh_Complex, DeckMesh_Simple |
| Trace Complex | false | false |
| Seed Point IDs | 빈 배열 | 빈 배열 |
| Required | true | true |

Height Mode가 Waypoint Reference이므로 저장되어 있는 Minimum/Maximum Floor Z는 이 모드의 범위 선택에 사용하지 않는다. 기존 문서의 LowerDeck 320–380과 달리 현재 ID 0 앵커 Z는 341이므로 계산 범위는 310–370이다.

공통 보행 설정: Obstacle Component Names는 DeckMesh_Complex/DeckMesh_Simple, Cell Size 75cm, Maximum Floor Slope 40도, Clearance Radius 35cm, Clearance Half Height 90cm, Maximum Step Height 45cm, Minimum Region Cells 6, Point Height Tolerance 100cm, Spawn Height Offset 30cm.

Walking Connections에는 LowerDeck → UpperDeck 하나가 저장되어 있고 Patrol Across Surfaces는 false다. 연결을 넣는 것만으로 계단 이동이 만들어지는 것은 아니다. 바닥과 단차가 실제로 이어져야 하며 계단 중간 높이를 샘플링할 수 있는 보행면이 필요하다. 층간 이동이 필요할 때 이 부분을 추가 구성한다.

현재 Draw Debug Area와 Draw Debug Connections가 모두 true다. 진단이 끝나고 표시가 필요 없으면 BP 컴포넌트의 해당 옵션을 끈다.

## 7. 함선 스킬 구성과 따로 연결할 에셋

현재 모든 모듈은 Allowed Navigation States = Orbit, Required/Blocked Owner Tags = 빈 값, Weight = 1, Use Only Once = false다. 아키타입의 선택 정책은 Highest Priority다. 활성화 가능한 모듈 중 우선순위가 높은 것을 선택하며 쿨다운 등으로 비활성인 스킬은 제외된다.

| 모듈 | Priority | Movement Policy | Ability Class | 현재 GA 쿨다운 |
| --- | --- | --- | --- | --- |
| Cannon | 10 | Continue Navigation | BP_GA_ES_CannonVolley | 0초: 대포별 DT 재장전으로 제어 |
| Charge | 20 | Override Navigation | BP_GA_ES_Charge | 20초 |
| Torpedo | 30 | Continue Navigation | BP_GA_ESLaunchTorpedo | 10초 |
| Obstacle | 20 | Continue Navigation | BP_GA_ES_DeployObstacle | 7초 |
| TimeStop | 20 | Continue Navigation | BP_GA_ES_TimeStop | 20초 |

Normal 1~4는 Cannon만, Charge는 Cannon+Charge, Torpedo_Obstacle은 Cannon+Torpedo+Obstacle, TimeStop은 Cannon+TimeStop을 사용한다.

에디터에서 스킬을 새로 구성할 때는 다음 세 단계를 모두 연결한다.

1. Skill Module의 Ability Class, 허용 항해 상태, Priority 또는 Weight/Sequence, Required/Blocked Tags, Use Only Once, Movement Policy를 설정한다.
2. Archetype의 Skill Modules 배열에 모듈을 넣고 Selection Policy를 선택한다.
3. 연결한 GA BP의 Class Defaults에서 생성 클래스·효과·수치·쿨다운을 설정한다. 모듈 자체에 모든 피해/쿨다운 수치가 들어 있지는 않다.

현재 BP에 저장된 주요 별도 설정:

| 스킬 | 현재 수치와 에셋 연결 |
| --- | --- |
| Cannon | 최소 고각 5도, 분산 타원 반장축 3,000cm/반단축 2,000cm, 공격자 방향 반쪽 가중치 0.66/반대쪽 0.33. 모듈의 Cannon Volley Settings에서 수정 |
| Charge | 추진 배율 10, 선회 배율 5, 조준 오차 1도, 조준 최대 10초, 종점 허용 150cm. 예고 클래스 BP_ES_ChargeTelegraph, 폭 1,000cm, World Z 20cm. Damage GE와 충돌 Niagara 지정 |
| Charge 피해 | 최소 접근 속도 1m/s, 최소 피해 5, 추가 1m/s당 5, 최대 피해 500. 일반 포격 피해 행을 바꾸는 것으로 이 피해 규칙이 바뀌지는 않음 |
| Torpedo | BP_ES_Torpedo, 포격 피해 배율 1.5, 목표 구간 비율 0.3, 3발을 3초 동안 발사, 최대 수명 30초 |
| Obstacle | BP_ES_ObstacleProjectile + BP_ES_Obstacle, 목표 비율 0.5, 목표 World Z 1,500cm, 회전 오프셋 Roll 90도, 속도 배율 1 |
| TimeStop | BP_ES_TimeStopField + BP_ES_TimeStopAimLine, 추적 충전 3초/고정 충전 2초, 반경 1,500cm, 시간정지 5초. 충전/탄도/폭발 Niagara가 별도 지정됨 |

생성되는 BP_ES_Torpedo/Obstacle/Field/AimLine/ChargeTelegraph를 새로 만드는 경우 그 BP의 Mesh·Collision·Material/Niagara와 관련 튜닝도 구성해야 한다. 기존 클래스가 연결된 현재 함선을 배치하는 것만으로 이 항목을 매번 재설정할 필요는 없다. 전체 GA 저장값과 생성 클래스 기본값은 조사 JSON에 포함했다.

일반 포격의 Cannon Lead Speed는 -1이면 실제 표적 속도, 0이면 리드를 끄고, 0보다 크면 표적 이동 방향에 지정 속도를 사용하는 방식이다. 한 척만 다르게 할 때 액터의 Override Cannon Lead Speed를 켜고 값을 지정한다. 51척 모두 현재 override는 꺼져 있다.

## 8. 사람 보스를 등장시키려면 필요한 추가 설정

현재 Lvl_CY의 EnemyShip 51척에서 BossEncounterComponent는 모두 Encounter Enabled=false / Boss Class=None / Boss Stats Row=비어 있음이다. **특수 함선 스킬과 갑판의 사람 보스는 별개 시스템이다.** BP_ES_Final이나 BP_ES_Mid_1이라는 이름만으로 이 컴포넌트에서 사람 보스가 생성되지 않는다.

보스를 추가할 함선 인스턴스에서 직접 설정한다.

| 필드 | 설정 내용 |
| --- | --- |
| Encounter Enabled | true |
| Boss Class | 생성할 AShipBossEnemy 파생 BP |
| Boss Stats Row | 원하는 보스 DT + 행. 지정하면 보스 BP 기본 스탯보다 우선 |
| Boss Spawn Point ID | 고유하게 정리한 보스 앵커 ID. 현재 의도된 ID는 12 |
| Encounter Trigger | Player Ship Sight 또는 Item Box Interaction |
| Required Story Node | 보스 등장 전에 도달해야 할 스토리 노드 |
| Stop After Story Node | 도달한 뒤 새 조우를 막을 스토리 노드 |
| Trigger Chest Spawn Point Component | Item Box Interaction 방식에서 실제 AChestSpawnPoint를 갖는 Child Actor Component 연결 |
| Enemy Item Box Component / Enemy Item Box | 해당 조우의 상자 연결이 필요한 경우 소유 Child Actor/레벨 상자 지정 |

현재 저장된 트리거 기본은 Player Ship Sight, Required Story Node는 GameStarted, Stop After Story Node는 MiddleBoss1Defeated다. 다음 중간보스용 인스턴스에 그대로 복사하면 중간보스1 완료 후 조우가 막힐 수 있으므로 해당 보스의 진행 단계로 바꾼다.

BP의 `TriggerBox` Child Actor에는 BP_TriggerChest가 있고 `ChestSpawnPoint` Child Actor에는 BP_ChestSpawnPoint가 있다. 현재 BossEncounter의 상자 참조들은 비어 있다. Item Box Interaction에서는 **TriggerBox라는 이름만 믿고 연결하지 말고 실제 AChestSpawnPoint 파생 클래스가 들어 있는 컴포넌트를 선택**한다.

보스 BP에서는 별도로 Default Stats Row, Encounter Balance Row, Summoned Enemy Class, 전투 BT/몽타주/공격 세트를 맞춘다. 기존 함선의 아키타입만 바꿔서는 사람 보스의 편성/스탯/연출이 구성되지 않는다.

## 9. Final 편대의 별도 등장 조건

`Squad ID = Final` 자체가 코드에서 사용하는 특별한 조건이다. `BP_ES_Normal_4_16`, `_17`, `_18`, `BP_ES_Final` 네 척은 UldolmokBattleQuestAccepted 도달 전에는 스토리 게이트로 숨김/비활성화되며, FinalBossDefeated 이후에도 게이트가 닫힌다. 개발용 최종전 테스트 출항 흐름으로 지정된 World에는 테스트용 예외가 있지만 일반 Lvl_CY 실행에 자동 적용된다고 가정하면 안 된다.

최종 보스 함선 사망으로 FinalBossDefeated를 완료하는 코드는 다음을 함께 검사한다.

- Squad ID가 Final.
- EnemyShipArchetype이 정확히 `/Game/Blueprints/Ship/Enemy_Ship/Data/Archetype/Elite/DA_ES_TimeStop.DA_ES_TimeStop`.
- UldolmokBattleQuestAccepted에 도달했고 아직 FinalBossDefeated가 아님.

현재 BP_ES_Final이 이 함선 설정을 충족한다. 아키타입을 복제한 다른 경로로 교체하거나 Squad ID를 바꾸면 일반 스킬이 작동해도 이 최종전 완료 조건은 달라질 수 있다. 새로운 최종전 데이터 구조를 만들 때 코드 조건과 함께 맞춘다.

## 10. 기존 BP가 이미 제공하는 구성과 자동 처리

현재 구성으로 새 함선을 배치하는 경우 재설정이 필요 없는 항목과, 모델을 새로 구성할 때 손봐야 하는 항목을 구분한다.

| 구성 | 현재 연결 | 다시 설정하는 경우 |
| --- | --- | --- |
| AI Controller / Auto Possess AI | BP_NavalAIController / Placed in World or Spawned | 새 Pawn/BP를 처음 만들 때 |
| AI Behavior Tree | BP_NavalAIController의 BT_NavalAI, TargetShip 키 | 다른 항해/패턴 BT를 만들 때 |
| 대포 | CannonPoint_1/2에 BP_Cannon, 서로 반대 측면 방향 | 대포 수·배치·사격 가능 방향을 바꿀 때 |
| 포탄 클래스 | BP_Cannon의 BP_CannonBall | 새 대포/포탄을 만들 때. 포탄 클래스가 비면 발사가 거부됨 |
| 대포 위치 | 로컬 (-270,-550,715) / (-270,550,715) | 모델을 바꾸거나 선체와 포구 간섭이 있을 때 |
| 선체 물리 Mesh | BuoyancyRoot → SM_Ship_Collision | 배 형상/충돌/부력을 바꿀 때 |
| 표시·피해 Mesh | SM_Ship_Visual / SM_Ship_Damage | 모델과 피격 영역을 바꿀 때 |
| 갑판 Mesh | DeckMesh_Simple → SM_Ship_Simple, Complex → SM_Ship_Complex | 층/난간/바닥 형상을 바꿀 때 |
| 상자 스폰 Child Actor | ChestSpawnPoint → BP_ChestSpawnPoint, 로컬 (-130,0,350) | 상자 개수·바닥 위치를 바꿀 때 |
| 기본 물리 힘 | Forward Force 2,000,000 / Turn Torque 6,000,000,000 | 선체 물리 튜닝을 새로 할 때. 일반 난이도 배율은 Spec Row 사용 |
| 롤 안정화/앵커/카메라/입력/복제 | 상속 BP와 C++의 설정 | 해당 기능을 바꾸는 경우 |

포격 속도/재장전은 BP_Cannon의 Fire Velocity=3000 / Fire Cooldown=1.5만 보는 것으로 결정되지 않는다. 소유 함선의 ASC 스탯(DT에서 적용한 값)을 사용하고 선체 체력에 따른 Cannon Cooldown Multiplier를 곱한다.

체력이 줄 때 승무원의 Strength/이동/공격속도 약화는 Project Settings의 Enemy Ship Weakening → Weakening Data를 통해 공통 적용된다. 현재 DefaultGame.ini에 DA_EnemyShipWeakening이 연결되어 있다. 이 약화 곡선을 조정하려면 그 Data Asset을 수정한다. 각 배에 수동 효과를 다시 넣을 필요는 없다.

추가로 자동 처리되는 내용: 소유 대포 목록 수집, 아키타입 스킬 부여, 함선 생성 위치 저장, 승무원 등록과 상자 경비 연결, 선체 체력에 따른 재장전 배율, 승무원 전멸 후 3배 피격 피해와 조종/앵커 권한, 거리 비활성화, 침몰 보상. SunkChestDefinition/ChestDefinition/RandomGroup 같은 이전 직렬화 필드는 현행 보상 구성의 설정 소스로 사용하지 않는다.

## 11. 현재 상태에서 먼저 확인할 순서

1. BP_EnemyShip에서 중복 Waypoint ID 12를 정리한다. 영향을 받는 참조도 같이 맞춘다.
2. 갑판 난이도를 함선 난이도와 맞출지 결정하고 Spawn Plan의 클래스·Stats Row·개수를 직접 구성한다. 현재는 모든 배가 T1 근접 2명이다.
3. 사람 보스를 사용할 함선만 BossEncounter를 활성화하고 Class/Stats/트리거/스토리/스폰 ID를 연결한다. 일반 함선은 비활성화한다. BP 기본 true + Class=None 조합도 새 배치 시 주의한다.
4. Normal_4_1의 6척 편대와 BP_ES_Normal_3_10의 특수 아키타입이 의도와 일치하는지 확인한다.
5. Final 4척을 시험할 때 해당 스토리 단계 또는 지원되는 개발 출항 흐름을 사용한다.
6. Compile/Save 후 각 BP/대표 배치 함선의 Validate Deck Waypoints를 실행한다. 서버 PIE에서 바닥·캡슐 여유·적 등장·층별 이동·포격·스킬·상자 보상·최종전 완료를 확인한다.

이번 검증은 저장값 조사와 정적 앵커 검사다. 실제 PIE, 바닥 그래프 재생성, 전투, 멀티플레이 동작을 검증한 결과는 아니다. 정적 검사 대표 3척은 모두 DuplicatePointId 오류 1개를 보고했다.

## 12. 51척 개별 목록

월드 위치는 cm, Yaw는 도다. 소수점은 표시를 위해 반올림했다. 모든 행의 갑판 편성은 T1 근접 2명, BossEncounter는 off다.

| Outliner Label | 내부 액터 이름 | Archetype | Squad ID | Zone | World X | World Y | World Z | Yaw |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| BP_ES_Final | BP_EnemyShip_C_10 | DA_ES_TimeStop | Final | FINAL | -225777.8 | -11380.6 | 0.0 | -130.0 |
| BP_ES_Mid_1 | BP_EnemyShip_C_6 | DA_ES_Charge | Normal_1_4 | MID1 | 225040.0 | 104600.0 | 0.0 | -90.0 |
| BP_ES_Normal_1_1 | BP_EnemyShip_C_2 | DA_ES_Normal_1 | Normal_1_1 | MID1 | 223843.9 | -50788.7 | 0.0 | -100.0 |
| BP_ES_Normal_1_2 | BP_EnemyShip_C_16 | DA_ES_Normal_1 | Normal_1_1 | MID1 | 216901.0 | -49564.5 | 0.0 | -100.0 |
| BP_ES_Normal_1_3 | BP_EnemyShip_C_1 | DA_ES_Normal_1 | Normal_1_1 | MID1 | 210157.9 | -48416.1 | 0.0 | -100.0 |
| BP_ES_Normal_1_4 | BP_EnemyShip_C_18 | DA_ES_Normal_1 | Normal_1_2 | MID1 | 175616.3 | 22099.9 | 0.0 | -100.0 |
| BP_ES_Normal_1_5 | BP_EnemyShip_C_19 | DA_ES_Normal_1 | Normal_1_2 | MID1 | 189429.7 | 19816.5 | 0.0 | -100.0 |
| BP_ES_Normal_1_6 | BP_EnemyShip_C_20 | DA_ES_Normal_1 | Normal_1_2 | MID1 | 182506.5 | 21037.3 | 0.0 | -100.0 |
| BP_ES_Normal_1_7 | BP_EnemyShip_C_21 | DA_ES_Normal_1 | Normal_1_4 | MID1 | 219690.0 | 109270.0 | 0.0 | -90.0 |
| BP_ES_Normal_1_8 | BP_EnemyShip_C_23 | DA_ES_Normal_1 | Normal_1_4 | MID1 | 230530.0 | 109010.0 | 0.0 | -90.0 |
| BP_ES_Normal_1_9 | BP_EnemyShip_C_22 | DA_ES_Normal_1 | Normal_1_4 | MID1 | 225040.0 | 113700.0 | 0.0 | -90.0 |
| BP_ES_Normal_1_10 | BP_EnemyShip_C_24 | DA_ES_Normal_1 | Normal_1_3 | MID1 | 251112.1 | 51750.7 | 0.0 | -100.0 |
| BP_ES_Normal_1_11 | BP_EnemyShip_C_25 | DA_ES_Normal_1 | Normal_1_3 | MID1 | 264925.4 | 49467.3 | 0.0 | -100.0 |
| BP_ES_Normal_1_12 | BP_EnemyShip_C_26 | DA_ES_Normal_1 | Normal_1_3 | MID1 | 258002.2 | 50688.1 | 0.0 | -100.0 |
| BP_ES_Normal_2_1 | BP_EnemyShip_C_17 | DA_ES_Normal_2 | Normal_2_1 | MID2 | 91965.2 | 18147.1 | 0.0 | -30.0 |
| BP_ES_Normal_2_2 | BP_EnemyShip_C_27 | DA_ES_Normal_2 | Normal_2_1 | MID2 | 88440.2 | 12041.7 | 0.0 | -30.0 |
| BP_ES_Normal_2_3 | BP_EnemyShip_C_28 | DA_ES_Normal_2 | Normal_2_1 | MID2 | 85747.6 | 5698.0 | 0.0 | -30.0 |
| BP_ES_Normal_2_4 | BP_EnemyShip_C_3 | DA_ES_Normal_2 | Normal_2_2 | MID2 | 104418.6 | 59015.4 | 0.0 | -80.0 |
| BP_ES_Normal_2_5 | BP_EnemyShip_C_4 | DA_ES_Normal_2 | Normal_2_2 | MID2 | 97475.7 | 57791.2 | 0.0 | -80.0 |
| BP_ES_Normal_2_6 | BP_EnemyShip_C_5 | DA_ES_Normal_2 | Normal_2_2 | MID2 | 90885.4 | 55776.2 | 0.0 | -80.0 |
| BP_ES_Normal_2_7 | BP_EnemyShip_C_29 | DA_ES_Normal_2 | Normal_2_3 | MID2 | 81195.5 | -73224.7 | 0.0 | 40.0 |
| BP_ES_Normal_2_8 | BP_EnemyShip_C_30 | DA_ES_Normal_2 | Normal_2_3 | MID2 | 73375.0 | -70609.8 | 0.0 | 40.0 |
| BP_ES_Normal_2_9 | BP_EnemyShip_C_31 | DA_ES_Normal_2 | Normal_2_3 | MID2 | 67355.9 | -67201.4 | -0.0 | 40.0 |
| BP_ES_Normal_3_1 | BP_EnemyShip_C_32 | DA_ES_Normal_3 | Normal_3_1 | MID3 | -54627.6 | -24938.9 | 0.0 | -30.0 |
| BP_ES_Normal_3_2 | BP_EnemyShip_C_33 | DA_ES_Normal_3 | Normal_3_1 | MID3 | -58152.6 | -31044.4 | 0.0 | -30.0 |
| BP_ES_Normal_3_3 | BP_EnemyShip_C_34 | DA_ES_Normal_3 | Normal_3_1 | MID3 | -60845.1 | -37388.0 | 0.0 | -30.0 |
| BP_ES_Normal_3_4 | BP_EnemyShip_C_35 | DA_ES_Normal_3 | Normal_3_2 | MID3 | 13530.6 | 17895.0 | 0.0 | -30.0 |
| BP_ES_Normal_3_5 | BP_EnemyShip_C_36 | DA_ES_Normal_3 | Normal_3_2 | MID3 | 10005.6 | 11789.5 | 0.0 | -30.0 |
| BP_ES_Normal_3_6 | BP_EnemyShip_C_37 | DA_ES_Normal_3 | Normal_3_2 | MID3 | 7313.0 | 5445.9 | 0.0 | -30.0 |
| BP_ES_Normal_3_7 | BP_EnemyShip_C_38 | DA_ES_Normal_3 | Normal_3_3 | MID3 | 22240.7 | 106353.1 | 0.0 | -80.0 |
| BP_ES_Normal_3_8 | BP_EnemyShip_C_39 | DA_ES_Normal_3 | Normal_3_3 | MID3 | 15297.9 | 105128.9 | 0.0 | -80.0 |
| BP_ES_Normal_3_9 | BP_EnemyShip_C_40 | DA_ES_Normal_3 | Normal_3_3 | MID3 | 8707.6 | 103113.9 | 0.0 | -80.0 |
| BP_ES_Normal_3_10 | BP_EnemyShip_C_7 | DA_ES_Torpedo_Obstacle | Normal_3_3 | MID3 | 16567.2 | 97930.0 | 0.0 | -80.0 |
| BP_ES_Normal_4_1 | BP_EnemyShip_C_43 | DA_ES_Normal_4 | Normal_4_1 | FINAL | -129237.0 | 27714.3 | 0.0 | -30.0 |
| BP_ES_Normal_4_2 | BP_EnemyShip_C_44 | DA_ES_Normal_4 | Normal_4_1 | FINAL | -132762.0 | 21608.8 | 0.0 | -30.0 |
| BP_ES_Normal_4_3 | BP_EnemyShip_C_45 | DA_ES_Normal_4 | Normal_4_1 | FINAL | -135454.6 | 15265.2 | 0.0 | -30.0 |
| BP_ES_Normal_4_4 | BP_EnemyShip_C_46 | DA_ES_Normal_4 | Normal_4_2 | FINAL | -225670.0 | 61307.0 | 0.0 | -10.0 |
| BP_ES_Normal_4_5 | BP_EnemyShip_C_47 | DA_ES_Normal_4 | Normal_4_2 | FINAL | -226894.2 | 54364.1 | 0.0 | -10.0 |
| BP_ES_Normal_4_6 | BP_EnemyShip_C_48 | DA_ES_Normal_4 | Normal_4_2 | FINAL | -227254.7 | 47482.1 | 0.0 | -10.0 |
| BP_ES_Normal_4_7 | BP_EnemyShip_C_49 | DA_ES_Normal_4 | Normal_4_1 | FINAL | -225670.0 | 61307.0 | 0.0 | -10.0 |
| BP_ES_Normal_4_8 | BP_EnemyShip_C_50 | DA_ES_Normal_4 | Normal_4_1 | FINAL | -226894.2 | 54364.1 | 0.0 | -10.0 |
| BP_ES_Normal_4_9 | BP_EnemyShip_C_51 | DA_ES_Normal_4 | Normal_4_1 | FINAL | -227254.7 | 47482.1 | 0.0 | -10.0 |
| BP_ES_Normal_4_10 | BP_EnemyShip_C_52 | DA_ES_Normal_4 | Normal_4_3 | FINAL | -88418.9 | 103780.7 | 0.0 | -60.0 |
| BP_ES_Normal_4_11 | BP_EnemyShip_C_53 | DA_ES_Normal_4 | Normal_4_3 | FINAL | -94524.4 | 100255.7 | 0.0 | -60.0 |
| BP_ES_Normal_4_12 | BP_EnemyShip_C_54 | DA_ES_Normal_4 | Normal_4_3 | FINAL | -100028.0 | 96108.2 | 0.0 | -60.0 |
| BP_ES_Normal_4_13 | BP_EnemyShip_C_55 | DA_ES_Normal_4 | Normal_4_4 | FINAL | -190679.0 | 154160.2 | 0.0 | -60.0 |
| BP_ES_Normal_4_14 | BP_EnemyShip_C_56 | DA_ES_Normal_4 | Normal_4_4 | FINAL | -196784.4 | 150635.2 | 0.0 | -60.0 |
| BP_ES_Normal_4_15 | BP_EnemyShip_C_57 | DA_ES_Normal_4 | Normal_4_4 | FINAL | -202288.1 | 146487.8 | 0.0 | -60.0 |
| BP_ES_Normal_4_16 | BP_EnemyShip_C_0 | DA_ES_Normal_4 | Final | FINAL | -217099.5 | -12161.6 | 0.0 | -130.0 |
| BP_ES_Normal_4_17 | BP_EnemyShip_C_8 | DA_ES_Normal_4 | Final | FINAL | -221985.9 | -7017.1 | 0.0 | -130.0 |
| BP_ES_Normal_4_18 | BP_EnemyShip_C_9 | DA_ES_Normal_4 | Final | FINAL | -227765.6 | -3263.9 | 0.0 | -130.0 |

조사 원본: `tmp/default_level_enemyships_audit.json`. 대표 정적 검증을 포함한 조사 로그: `tmp/default_level_enemyships_audit.log`.

주요 근거: Config/DefaultEngine.ini, Config/DefaultGame.ini, Source/Enemy/Public/ShipAI/EnemyShip.h, Source/Enemy/Private/ShipAI/EnemyShip.cpp, EnemyShipArchetypeData.h/.cpp, EnemyShipPatternRuntimeComponent.cpp, ShipSwarmSubsystem.cpp, GA_EnemyShipCannonVolley.cpp, DeckEnemySpawnerComponent.h/.cpp, DeckSpawnAnchorValidator.cpp, DeckWalkAreaComponent.h, BossEncounterComponent.h/.cpp, Source/ClassFeature/Public/ItemSpawn/LootSpawnPoint.h.
