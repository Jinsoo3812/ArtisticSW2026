# 데이터 기반 보상 상자 시스템

## 1. 결론

상자 Actor는 기존 `AStorageChest` 하나만 사용한다.

- 랜덤 상자와 보호 상자를 별도 상자 클래스로 나누지 않는다.
- `ChestGuardComponent`, `GuardedRewardChest`, 별도 Reward 모듈을 만들지 않는다.
- 상자 종류, 내용물, 배치 위치, 경비병, 소속 배를 데이터와 `AChestSpawnPoint`에서 조합한다.
- 적은 자신이 지키는 상자나 보상을 알지 않는다.
- `AStorageChest`가 자신에게 할당된 경비병과 배의 상태만 관찰한다.

기존 침몰 보상은 그대로 별개다.

```text
적 배 갑판 상자
  = 레벨에 배치된 보호 상자
  = 갑판 적 전멸 시 해금
  = 배가 먼저 침몰하면 획득 실패

적 배 침몰 상자
  = AEnemyShip::DropAtDeathLocation()의 기존 보상
  = 배 침몰 시 바다에 별도로 생성
```

따라서 모든 보상을 얻으려면 갑판 적을 전멸시키고 갑판 상자를 획득한 뒤, 배를 침몰시켜 기존 침몰 상자도 획득해야 한다.

## 2. 데이터 구조

### `UChestDefinition`

재사용 가능한 “무슨 상자인가”를 정의하는 Data Asset이다.

| 필드 | 역할 |
|---|---|
| `ChestClass` | 생성할 `AStorageChest` Blueprint/Class |
| `LootTable` | 기존 `FChestInitialLootRow` Data Table |
| `RollCount` | 가중치 추첨 횟수 |
| `SlotCount` | 저장 슬롯 수 |
| `ColumnCount` | UI 열 수 |
| `bEnablePhysicsAndBuoyancy` | 바다에 뜨는 상자인지 여부 |

드랍 테이블 행은 기존 형식을 그대로 사용한다.

```text
ItemTag / MinCount / MaxCount / Weight
```

추첨은 Seed 기반, 가중치 복원 추출이다. 같은 Seed와 같은 테이블이면 같은 초기 내용물이 나온다.

### `URandomChestGroup`

여러 랜덤 스폰 지점을 하나의 군집으로 묶는 Data Asset이다.

| 필드 | 역할 |
|---|---|
| `ChestDefinition` | 이 군집에서 생성할 상자 |
| `SpawnCount` | 군집의 지점 중 활성화할 개수 |

예:

```text
DA_RandomChestGroup_MidBoss
  ChestDefinition = DA_Chest_LowMaterial
  SpawnCount = 2

DA_RandomChestGroup_FinalBoss
  ChestDefinition = DA_Chest_HighMaterial
  SpawnCount = 1
```

## 3. 레벨 배치 방식

레벨에는 모든 상자 위치를 `AChestSpawnPoint`로 배치한다.

### 랜덤 상자

```text
SpawnMode = Random
RandomGroup = 원하는 URandomChestGroup
PointWeight = 같은 군집 안에서 선택될 상대 가중치
```

- 잠기지 않는다.
- 같은 `RandomGroup`을 선택한 지점끼리 한 군집이다.
- `SpawnCount`개를 가중치 기반 비복원 선택한다.

### 보호 상자

```text
SpawnMode = Guarded
ChestDefinition = 생성할 상자 정의
GuardCharacters = 이 상자를 지키는 적 인스턴스 목록
OwningShip = 섬 상자는 비움, 배 상자는 해당 배 지정
```

- Box 범위나 Squad를 이용한 자동 수집은 하지 않는다.
- 경비병은 상자 스폰 지점에서 하나씩 명시적으로 지정한다.
- `OwningShip`의 유무로 섬/배를 구분하므로 별도 Island/Ship enum은 없다.
- 배 상자의 스폰 지점과 상자 위치는 Level Outliner에서 해당 배의 자식으로 두는 것을 권장한다.
- 런타임에 생성된 상자도 지정한 배에 Attach된다.

### 기존 Zone 스폰과의 호환

`SpawnMode = Legacy`인 기존 지점은 기존 `ALootZoneSpawnManager`가 계속 처리한다.

`Random`과 `Guarded` 지점은 `AGlobalLootSpawnManager`가 먼저 처리하고, 기존 Zone Manager는 이 지점들을 건너뛴다. 따라서 현재의 느슨한 아이템/기존 상자 배치를 깨지 않는다.

## 4. 런타임 흐름

### 레벨 시작

```text
AGlobalLootSpawnManager::InitializeLevelLoot()
  -> InitializeDataDrivenChests()
     -> 모든 Guarded 지점 생성
     -> RandomGroup별 SpawnCount만큼 지점 선택 및 생성
  -> 기존 Zone 예산/스폰 처리
```

상자는 Deferred Spawn으로 생성한다.

```text
상자 Actor 생성 예약
  -> ChestDefinition 적용 및 내용물 추첨
  -> 경비병/소속 배 적용
  -> FinishSpawning
```

이 순서로 보호 상자는 `BeginPlay` 시점부터 이미 잠겨 있다.

### 섬 보호 상자

```text
초기: Locked
경비병 한 명 DeathStarted: 생존 경비병 목록에서 제거
마지막 경비병 DeathStarted: Unlocked
```

- 경비병의 `UBaseHealthComponent::OnDeathStarted`를 구독한다.
- 이미 죽은 경비병은 생존 목록에 넣지 않는다.
- 유효한 경비병이 한 명도 설정되지 않은 보호 상자는 설정 오류로 보고 잠긴 채 유지한다.
- Despawn이나 단순 Actor 제거를 처치로 간주하지 않는다.

### 배 보호 상자

섬 상자의 규칙에 다음 조건이 추가된다.

```text
갑판 적 전멸
  -> Unlocked

소속 배 DeathStarted
  -> GuardFailed = true
  -> Locked
  -> 경비병 사망 구독 해제

소속 배 OnDestroyed
  -> 상자 Destroy
```

배가 침몰하기 시작하면 갑판 상자를 아직 먹지 못한 것으로 간주해 영구 잠근다. 이후 남은 경비병을 처치해도 다시 열리지 않는다.

상자를 즉시 없애지 않고 배의 실제 `OnDestroyed`에 맞춰 없애므로, 기존 `DestroyAfterDeathDelay`와 자동으로 같은 타이밍을 사용한다.

## 5. 잠금과 서버 권한

`AStorageChest`가 다음 상태를 직접 가진다.

```text
bLocked
bGuardFailed
bRequiresGuardClear
GuardCharacters
OwningShip
```

`bLocked`, `bGuardFailed`, 물리 모드는 복제한다.

잠긴 동안에는 다음 경로를 모두 서버에서 차단한다.

- 상호작용으로 Storage 열기
- Storage 슬롯 클릭/이동
- 인벤토리에서 Storage로 빠른 이동
- Storage에서 인벤토리로 빠른 이동
- Storage 검색 타이머 진행

열린 상자가 다시 잠기면 서버가 해당 플레이어의 Storage UI를 닫고 검색 타이머를 취소한다.

## 6. 모듈 의존성

새 모듈과 새 순환 의존성은 없다.

```text
Enemy -> ClassFeature   (기존 의존성)
ClassFeature -> GASCore (기존 의존성)
ClassFeature -> WaterAndShip (기존 의존성)
```

`AStorageChest`는 다음 범용 타입만 참조한다.

- `ABaseCharacter`와 `UBaseHealthComponent`: `GASCore`
- `AShip`: `WaterAndShip`

`ABaseEnemy`, `AEnemyShip`을 참조하지 않으므로 `ClassFeature -> Enemy` 의존성은 생기지 않는다. Enemy 코드 변경도 필요 없다.

## 7. 변경 파일

### 데이터/스폰

- `Source/ClassFeature/Public/ItemSpawn/ChestSpawnData.h`
- `Source/ClassFeature/Private/ItemSpawn/ChestSpawnData.cpp`
- `Source/ClassFeature/Public/ItemSpawn/LootSpawnPoint.h`
- `Source/ClassFeature/Private/ItemSpawn/LootSpawnPoint.cpp`
- `Source/ClassFeature/Public/ItemSpawn/GlobalLootSpawnManager.h`
- `Source/ClassFeature/Private/ItemSpawn/GlobalLootSpawnManager.cpp`
- `Source/ClassFeature/Private/ItemSpawn/LootZoneSpawnManager.cpp`

### 잠금/보호 조건

- `Source/ClassFeature/Public/Storage/StorageChest.h`
- `Source/ClassFeature/Private/Storage/StorageChest.cpp`
- `Source/ClassFeature/Public/BasePlayerController.h`
- `Source/ClassFeature/Private/BasePlayerController.cpp`

### 테스트

- `Source/ClassFeature/Private/Tests/ChestSpawnAndGuardTests.cpp`

## 8. 자동화 테스트

`ArtisticSW.Chest.DataDrivenSpawnGroups`

- 서로 다른 랜덤 군집을 분리해서 집계
- 군집별 `SpawnCount`만큼만 활성화
- 드랍 테이블 아이템과 수량 생성
- 슬롯/열 설정 적용
- 랜덤 상자가 잠기지 않음

`ArtisticSW.Chest.GuardedUnlockAndShipFailure`

- 섬 상자가 초기 잠김
- 일부 경비병 사망 시 잠금 유지
- 마지막 경비병 사망 시 해금
- 배 사망 시 영구 실패와 재잠금
- 배 사망 후 경비병 사망으로 재해금되지 않음
- 배의 실제 Destroy 시 갑판 상자도 Destroy

`ArtisticSW.Chest.MapPlacedLogic`

- `/Game/Tests/ChestSystem/ChestSystem_Test_Level` 실제 맵 로드
- 맵에 저장된 두 Random Group의 생성 개수 확인
- 맵에 저장된 섬/배 보호 상자의 경비병 참조 확인
- 보호 상자의 초기 잠금과 배 상자 물리 비활성화 확인

`ArtisticSW.Tools.BuildChestVisualTestMap`

- 원본 `/Game/New/Level/Test_Level`을 읽어 테스트 맵을 재생성
- 랜덤 저등급 지점 3개, 랜덤 고등급 지점 2개 배치
- 섬 경비병 2명과 보호 상자 지점 배치
- 적 배, 갑판 경비병, 배 보호 상자 지점 배치
- `ChestSystem_Automation` Outliner 폴더에 모든 테스트 Actor 정리

Editor World에서는 런타임 Dynamic Delegate를 게임과 동일하게 실행할 수 없으므로 역할을 분리한다.

- 실제 맵 테스트: 배치/참조/스폰 개수/초기 상태 검증
- Game World 테스트: 경비병 사망/해금/배 실패/동시 제거 검증

## 9. 이번 범위에서 만들지 않은 것

- Box/거리 기반 경비병 자동 검색
- Squad/Spawn Group 자동 연결
- 범용 조건 그래프
- 별도 Reward Subsystem/Manager
- 개인별 Loot
- Pity/천장 시스템
- 상자 상태 세이브/로드

필요가 실제로 생길 때 현재의 `GuardCharacters`, `RandomGroup`, `UChestDefinition`을 확장하는 편이 단순하다.
