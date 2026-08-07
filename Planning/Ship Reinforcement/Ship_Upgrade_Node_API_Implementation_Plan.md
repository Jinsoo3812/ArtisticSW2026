# 배 강화 노드 및 UI API 구현 계획

## 1. 목적

기획자가 플레이어 배의 강화 노드 그래프와 각 노드의 스탯 변경량을 자유롭게 설계할 수 있게 한다.

UI 담당자는 Data Asset, Gameplay Ability System(GAS), 네트워크 RPC, 저장 데이터를 직접 다루지 않고 공개된 Blueprint API만으로 다음 기능을 구현할 수 있어야 한다.

- 전체 강화 노드 배치
- 노드 사이의 선행 관계 표시
- 노드의 잠금/활성화 가능/활성화 완료 상태 표시
- 선택한 노드의 설명과 스탯 변경량 표시
- 활성화 전후 스탯 미리보기
- 노드 활성화 요청과 성공/실패 결과 처리

활성화한 노드는 플레이어의 영구 진행 데이터로 취급한다. 이후 전투 맵에 진입해 플레이어 배가 생성되거나 배정될 때마다 해당 강화 결과가 배의 기본 스탯에 포함되어야 한다.

Enemy 배는 플레이어 강화 데이터를 사용하지 않는다. Enemy 배는 계속 자신의 배 스탯 Data Table 행을 기본값으로 사용한다.

---

## 2. 현재 구현 검토 결과

### 2.1 현재 배 기본 스탯

현재 `FShipStatRow`에는 다음 다섯 값이 있다.

| 필드 | 의미 |
| --- | --- |
| `MaxHealth` | 배 최대 체력 |
| `ShipSpeedMultiplier` | WS 추진과 AD 회전에 함께 적용되는 통합 배율 |
| `CannonDamage` | 대포알 피해량 |
| `CannonFireCooldown` | 다음 발사까지 걸리는 시간(초) |
| `CannonballSpeed` | 대포알 초기 비행 속도 |

근거: `Source/WaterAndShip/Public/Ship.h`의 `FShipStatRow`.

실제 `DT_ShipStat`에는 최소한 다음 행이 존재한다.

- `PlayerShip`
- `EnemyShip_Normal`

### 2.2 WS와 AD가 통합되어 있는 문제

현재 `ShipSpeedMultiplier` 하나가 비동기 물리 입력의 `SpeedMultiplier`로 전달된다.

그 값은 다음 두 계산에 동시에 사용된다.

```cpp
AppliedControlForce = Forward * CachedForwardForce
    * MovementInput_Internal * CachedSpeedMultiplier;

AppliedControlTorque = FVector(0.f, 0.f,
    CachedTurnTorque * SteeringInput_Internal * CachedSpeedMultiplier);
```

따라서 현재 설계에서는 WS 추진 성능만 올리거나 AD 회전 성능만 올리는 노드를 만들 수 없다.

근거:

- `Source/WaterAndShip/Private/Ship.cpp`
- `Source/WaterAndShip/Public/ShipPhysicsAsync.h`
- `Source/WaterAndShip/Private/ShipPhysicsAsync.cpp`

### 2.3 현재 기본값 적용 시점

`AShip::BeginPlay()`에서 서버가 `InitializeDefaultAttributes()`를 호출한다. 이 함수는 `ShipStatTable`의 `ShipStatRowName` 행을 읽어 Ship의 AttributeSet을 초기화한다.

현재는 플레이어의 영구 강화 상태를 조회하거나 기본값에 합산하는 경로가 없다.

### 2.4 현재 스탯 소비 경로

- 배 체력: `UBaseAttributeSet`의 `Health`, `MaxHealth`
- WS/AD 통합 이동: `UShipAttributeSet::ShipSpeedMultiplier`
- 대포 피해: `UShipAttributeSet::CannonDamage`
- 다음 발사까지의 시간: `UShipAttributeSet::CannonFireCooldown`
- 대포알 비행 속도: `UShipAttributeSet::CannonballSpeed`

대포는 발사 시 Ship AttributeSet에서 피해, 쿨다운, 탄속을 읽는다.

### 2.5 대포 서버 권위 보완 필요

현재 클라이언트가 `ServerFire` RPC에 Damage와 Speed를 전달하고 서버가 그 값을 사용한다. 플레이어 강화 수치를 신뢰할 수 있게 만들려면 서버가 실제 Ship AttributeSet에서 피해, 탄속, 쿨다운을 다시 조회해야 한다.

강화 기능 구현과 함께 다음을 수정한다.

- 클라이언트는 발사 요청과 필요한 조준 정보만 전송한다.
- 서버가 Ship AttributeSet에서 피해량과 탄속을 결정한다.
- 서버도 발사 쿨다운을 독립적으로 검사한다.
- 클라이언트가 전달한 임의 스탯 값은 사용하지 않는다.

---

## 3. 목표 배 스탯 정의

배의 최종 게임플레이 스탯은 다음 여섯 종류로 정의한다.

```cpp
UENUM(BlueprintType)
enum class EShipStatType : uint8
{
    CannonDamage,
    CannonFireCooldown,
    CannonballSpeed,
    MaxHealth,
    ForwardPropulsion,
    TurnSpeed
};
```

런타임 계산과 UI 전달에는 하나의 스냅샷 구조를 사용한다.

```cpp
USTRUCT(BlueprintType)
struct FShipStatSnapshot
{
    GENERATED_BODY()

    float CannonDamage = 20.0f;
    float CannonFireCooldownSeconds = 2.0f;
    float CannonballSpeed = 3000.0f;
    float MaxHealth = 100.0f;
    float ForwardPropulsionMultiplier = 1.0f;
    float TurnTorqueMultiplier = 1.0f;
};
```

### 명명 주의사항

현재 선박 이동은 최대속도를 직접 제한하는 구조가 아니다.

- WS 스탯은 전진 힘에 곱해지는 추진 배율이다.
- AD 스탯은 회전 토크에 곱해지는 회전 배율이다.

따라서 내부 필드는 실제 물리 의미에 맞춰 `ForwardPropulsionMultiplier`, `TurnTorqueMultiplier`로 둔다. UI에서는 각각 `배 WS 추진속도`, `배 AD 회전속도`로 표시할 수 있다.

향후 진짜 최고 직선 속도나 최고 각속도가 필요하다면 별도의 속도 제한 및 물 저항 설계가 필요하다.

### Data Table 마이그레이션

기존 `ShipSpeedMultiplier`를 다음 두 값으로 분리한다.

```cpp
float ForwardPropulsionMultiplier = 1.0f;
float TurnTorqueMultiplier = 1.0f;
```

마이그레이션 시 기존 통합 값을 두 새 필드에 동일하게 복사한다. 이후 기획자가 `PlayerShip`, `EnemyShip_Normal` 등 각 행의 값을 개별 조정한다.

기존 필드는 즉시 이름만 바꿔 바이너리 자산의 값을 잃게 하지 않는다. 에디터 마이그레이션으로 값을 복사하고 Data Table 자산을 저장한 뒤 구 필드를 제거한다.

---

## 4. 강화 노드 기획 데이터

### 4.1 저장 형식

강화 그래프 전체는 `UShipUpgradeTreeDataAsset`으로 정의하는 것을 권장한다.

Data Table보다 Data Asset이 적합한 이유는 다음과 같다.

- 노드 하나에 여러 Modifier를 중첩 배열로 넣기 쉽다.
- 선행 노드 배열과 UI 좌표를 한 자산에서 관리할 수 있다.
- `IsDataValid()`로 그래프 전체의 순환 참조와 ID 중복을 검사할 수 있다.
- UI는 Data Asset을 직접 읽지 않고 런타임 API가 가공한 View만 받는다.

### 4.2 노드 정의

```cpp
USTRUCT(BlueprintType)
struct FShipUpgradeNodeDefinition
{
    GENERATED_BODY()

    FName NodeId;
    FText DisplayName;
    FText Description;
    TSoftObjectPtr<UTexture2D> Icon;
    FVector2D GraphPosition;
    TArray<FName> PrerequisiteNodeIds;
    TArray<FShipStatModifier> StatModifiers;
    TArray<FCraftingItemStack> ActivationCosts;
};
```

`NodeId`는 배열 인덱스나 표시 이름과 분리된 영구 식별자다. 저장 데이터는 이 ID만 보관하므로 출시 이후 표시 이름이나 UI 위치를 바꿔도 저장 파일이 깨지지 않아야 한다.

`PrerequisiteNodeIds`는 0개 이상을 자유롭게 지정할 수 있으며 **AND 조건**이다. 즉 N개의 상위 노드를 지정했다면 N개가 모두 `Active`일 때만 현재 노드가 `Available`이 된다. 배열이 비어 있으면 루트 노드다.

`ActivationCosts`도 0개 이상 설정할 수 있다. 각 원소는 기존 Item Crafting API와 같은 `FCraftingItemStack(ItemTag, Quantity)`을 사용한다. 배열이 비어 있으면 무료 노드이며, 같은 ItemTag가 여러 번 들어오면 런타임에서 합산한 뒤 한 번의 원자적 트랜잭션으로 처리한다.

### 4.3 스탯 Modifier

```cpp
UENUM(BlueprintType)
enum class EShipStatModifierOperation : uint8
{
    AddFlat,
    AddPercent
};

USTRUCT(BlueprintType)
struct FShipStatModifier
{
    GENERATED_BODY()

    EShipStatType StatType;
    EShipStatModifierOperation Operation;
    float Value = 0.0f;
};
```

예시:

| 노드 효과 | Operation | Value |
| --- | --- | ---: |
| 대포 피해 +10 | `AddFlat` | `10.0` |
| 다음 발사까지의 시간 0.2초 감소 | `AddFlat` | `-0.2` |
| 대포알 속도 15% 증가 | `AddPercent` | `0.15` |
| 최대 체력 +100 | `AddFlat` | `100.0` |
| WS 추진 성능 20% 증가 | `AddPercent` | `0.2` |
| AD 회전 성능 10% 증가 | `AddPercent` | `0.1` |

`Override` 연산은 여러 노드의 순서에 따라 결과가 달라지기 쉬우므로 1차 구현에서는 제공하지 않는다. 반드시 필요해질 때 우선순위 규칙과 함께 추가한다.

### 4.4 기획 데이터 검증

`UShipUpgradeTreeDataAsset::IsDataValid()`에서 다음을 검사한다.

- `NodeId`가 비어 있는 노드
- 중복 `NodeId`
- 존재하지 않는 선행 노드 ID
- 자기 자신을 선행 조건으로 지정한 노드
- 그래프 순환 참조
- 같은 선행 노드 ID의 중복 지정
- Modifier가 하나도 없는 노드
- 유효하지 않은 재료 ItemTag
- 0 이하의 재료 수량
- 같은 재료 합산 결과의 `int32` 초과
- 값이 0인 Modifier
- 지원하지 않는 Operation
- 예상 클램프를 항상 넘기는 값

에디터에서 자산을 저장하거나 Validate Assets를 실행할 때 기획자가 즉시 문제를 확인할 수 있어야 한다.

---

## 5. 최종 스탯 계산 규칙

최종 스탯은 순수 계산 함수로 만든다. Actor, World, 네트워크 상태를 참조하지 않아야 자동 테스트와 UI 미리보기에 같은 계산기를 사용할 수 있다.

```text
ShipStatRow 기본값
    + 활성 노드의 AddFlat 합계
    + 활성 노드의 AddPercent 합계
    → 유효 범위 Clamp
    → FShipStatSnapshot
```

권장 계산식:

```text
Final = (Base + Sum(Flat)) * (1 + Sum(Percent))
```

모든 노드는 `NodeId` 기준으로 정렬한 후 처리하여 서버, 클라이언트, UI 미리보기에서 결정적인 결과를 얻는다. 현재 두 연산은 합산 방식이므로 정렬에 따라 수치가 달라지지는 않지만 로그와 향후 확장성을 위해 순서를 고정한다.

최소값 정책:

| 스탯 | 최소값 |
| --- | ---: |
| `MaxHealth` | `1.0` |
| `CannonDamage` | `0.0` |
| `CannonFireCooldownSeconds` | `0.05` |
| `CannonballSpeed` | `0.0` |
| `ForwardPropulsionMultiplier` | `0.0` |
| `TurnTorqueMultiplier` | `0.0` |

Data Asset 검증은 잘못된 결과를 경고하고, 런타임 계산기는 저장 데이터 변조나 이전 버전 데이터에도 안전하도록 최종 Clamp를 다시 수행한다.

---

## 6. 런타임 상태와 영구 저장

### 6.1 역할 분리

| 계층 | 책임 |
| --- | --- |
| `UShipUpgradeTreeDataAsset` | 기획 원본과 그래프 정의 |
| `UShipUpgradeComponent` | 서버 권위 활성화 처리, 활성 노드 복제, UI API |
| `UShipUpgradeSaveGame` 또는 프로필 저장소 | 맵과 세션을 넘어 유지되는 활성 NodeId 저장 |

### 6.2 PlayerState 컴포넌트

`UShipUpgradeComponent`는 `ABasePlayerState`의 기본 서브오브젝트로 생성한다.

이 컴포넌트와 관련 타입은 `WaterAndShip` 모듈에 둘 수 있다. `ClassFeature`가 이미 `WaterAndShip`에 의존하므로 `ABasePlayerState`가 컴포넌트를 생성할 수 있고, `AShip`도 모듈 순환 의존 없이 같은 컴포넌트를 조회할 수 있다.

컴포넌트 책임:

- 활성화된 `NodeId` 목록 보관
- Owner 클라이언트로 상태 복제
- 노드 존재 여부와 선행 조건 검증
- 활성화 재료 보유량 조회와 원자적 차감
- 멱등적인 활성화 처리
- 서버의 영구 저장소 갱신
- 현재/미리보기 스탯 계산
- UI 이벤트 발생

저장 데이터에는 계산된 최종 수치를 저장하지 않는다. 활성화된 `NodeId`만 저장하고 로드 시 현재 기획 Data Asset으로 다시 계산한다.

### 6.3 맵 이동과 접속

권장 흐름:

1. 플레이어 프로필 또는 SaveGame에서 활성 NodeId를 로드한다.
2. 서버가 PlayerState의 `UShipUpgradeComponent`를 초기화한다.
3. 활성 NodeId가 Owner 클라이언트로 복제된다.
4. 전투 맵에서 플레이어 배를 스폰하거나 배정한다.
5. 서버가 기본 Ship DT 행과 활성 노드를 합산한다.
6. 최종 스냅샷을 플레이어 배에 적용한다.

초기 구현이 싱글플레이/Listen Server 전용이어도 서버 권위 경계를 유지한다. 향후 Dedicated Server나 온라인 프로필 저장소로 바꿀 때 UI 및 Ship 적용 API를 변경하지 않도록 저장소 호출을 별도 인터페이스로 격리한다.

---

## 7. 플레이어 배 적용

### 7.1 적용 API

`AShip`에 서버 전용 진입점을 추가한다.

```cpp
void InitializeBaseShipStats();
bool ApplyPlayerUpgradeSnapshot(APlayerState* InPlayerState);
void ResetToBaseShipStats();
```

처리 흐름:

```text
InitializeBaseShipStats
    → ShipStatRow 읽기
    → 기본 FShipStatSnapshot 생성

ApplyPlayerUpgradeSnapshot
    → PlayerState의 UShipUpgradeComponent 조회
    → 기본값 + 활성 노드 계산
    → AttributeSet과 물리 입력이 소비할 값 갱신
```

### 7.2 적용 시점

다음 두 진입점을 지원한다.

- 전투 GameMode가 플레이어 배를 스폰하거나 PlayerState에 배정하는 순간
- 현재 승선 방식에서 PlayerController가 배를 Possess하는 순간

전투 배가 명시적으로 플레이어에게 배정되는 구조가 가장 안전하다. Enemy 태그나 현재 Controller 종류만으로 플레이어 배를 추측하지 않는다.

### 7.3 공유 승선 배 주의사항

현재는 플레이어가 월드의 배에 승선하여 해당 Ship Pawn을 Possess할 수 있다.

만약 같은 배를 여러 플레이어가 번갈아 탈 수 있다면 다음 중 정책을 확정해야 한다.

1. 배 자체가 특정 플레이어 소유라면 소유 PlayerState의 강화만 계속 적용한다.
2. 탑승자의 강화가 임시 적용된다면 하선 시 `ResetToBaseShipStats()`를 호출한다.

초기 권장안은 전투 진입 시 플레이어 전용 배를 명시적으로 배정하는 것이다. 그러면 다른 플레이어에게 강화 스탯이 남는 문제를 피할 수 있다.

### 7.4 체력 적용 정책

전투 맵 진입 시에는 최종 `MaxHealth`를 적용한 뒤 `Health = MaxHealth`로 시작한다.

전투 도중 노드를 활성화하는 기능은 1차 범위에서 제외한다. 강화 UI가 비전투 맵에 있다는 전제로 다음 전투 배 생성 시 반영한다.

향후 전투 도중 강화가 필요해지면 최대 체력 변경 시 다음 중 하나를 별도 정책으로 선택한다.

- 현재 체력 비율 유지
- 증가량만큼 현재 체력도 회복
- 최대 체력만 변경

---

## 8. Enemy 배 분리

Enemy Ship은 다음 규칙을 유지한다.

- 자신의 `ShipStatTable`과 `ShipStatRowName`만 읽는다.
- PlayerState의 `UShipUpgradeComponent`를 조회하지 않는다.
- 플레이어 노드 활성화 이벤트를 구독하지 않는다.
- 스폰 시 기본 DT 값으로 AttributeSet을 초기화한다.

단, 공통 `FShipStatRow`가 6종으로 바뀌므로 Enemy 행에도 다음 두 값을 입력해야 한다.

- `ForwardPropulsionMultiplier`
- `TurnTorqueMultiplier`

이는 플레이어 강화 시스템에 Enemy가 참여한다는 의미가 아니라 공통 기본 스탯 스키마의 마이그레이션이다.

---

## 9. UI 담당자용 Blueprint API

### 9.1 UI 전용 상태 타입

```cpp
UENUM(BlueprintType)
enum class EShipUpgradeNodeState : uint8
{
    Locked,
    Available,
    Active
};
```

```cpp
USTRUCT(BlueprintType)
struct FShipUpgradeNodeView
{
    GENERATED_BODY()

    FName NodeId;
    FText DisplayName;
    FText Description;
    TSoftObjectPtr<UTexture2D> Icon;
    FVector2D GraphPosition;
    TArray<FName> PrerequisiteNodeIds;
    EShipUpgradeNodeState State;
    TArray<FShipStatChangeView> StatChanges;
    TArray<FShipUpgradeMaterialView> MaterialCosts;
    bool bHasEnoughMaterials;
};
```

`FShipUpgradeMaterialView`는 `ItemTag`, `DisplayName`, `Icon`, `OwnedQuantity`, `RequiredQuantity`, `bEnough`을 제공한다. UI는 Item Data나 Inventory를 직접 조회하지 않는다.

노드 `State`는 트리 의존성만 표현한다. 모든 상위 노드가 활성화되면 재료가 부족해도 `Available`이며, 실제 버튼 가능 여부는 `CanActivateNode` 또는 `bHasEnoughMaterials`로 판단한다. 이 규칙으로 “상위 노드 잠김”과 “재료 부족”을 UI에서 구분할 수 있다.

`FShipStatChangeView`에는 UI가 숫자의 의미를 재해석하지 않도록 다음을 포함한다.

- 스탯 타입
- 현지화된 표시 이름
- 적용 전 값
- 적용 후 값
- 차이 값
- 단위
- 증가가 이득인지 감소가 이득인지
- 완성된 표시 문구

예: `다음 발사까지의 시간 2.0초 → 1.8초 (-0.2초)`.

### 9.2 조회 함수

```cpp
UFUNCTION(BlueprintPure)
TArray<FShipUpgradeNodeView> GetAllNodeViews() const;

UFUNCTION(BlueprintPure)
bool GetNodeView(FName NodeId, FShipUpgradeNodeView& OutView) const;

UFUNCTION(BlueprintPure)
EShipUpgradeNodeState GetNodeState(FName NodeId) const;

UFUNCTION(BlueprintPure)
bool IsNodeActive(FName NodeId) const;

UFUNCTION(BlueprintPure)
bool CanActivateNode(FName NodeId, FText& OutReason) const;

UFUNCTION(BlueprintPure)
FShipStatSnapshot GetCurrentShipStats() const;

UFUNCTION(BlueprintPure)
bool GetStatsAfterActivating(
    FName NodeId,
    FShipStatSnapshot& OutPreviewStats) const;

UFUNCTION(BlueprintPure)
TArray<FShipStatChangeView> GetNodeStatChanges(FName NodeId) const;

UFUNCTION(BlueprintPure)
TArray<FShipUpgradeMaterialView> GetNodeMaterialCosts(FName NodeId) const;

UFUNCTION(BlueprintPure)
bool HasRequiredMaterials(FName NodeId, FText& OutReason) const;

UFUNCTION(BlueprintCallable)
void RefreshUpgradeData();
```

### 9.3 활성화 요청

```cpp
UFUNCTION(BlueprintCallable)
void RequestActivateNode(FName NodeId);
```

네트워크 RPC는 내부 구현으로 숨긴다. Blueprint 함수가 즉시 `bool` 성공을 반환하게 만들지 않는다. 서버 응답이 비동기이므로 결과는 이벤트로 전달한다.

### 9.4 이벤트

```cpp
OnUpgradeDataReady
OnNodeStateChanged(NodeId, NewState)
OnNodeActivationResult(NodeId, Result, Message)
OnShipStatsChanged(NewStats)
OnUpgradeDataChanged
```

활성화 결과 예시:

- 성공
- 이미 활성화됨
- 알 수 없는 NodeId
- 선행 노드 미충족
- 서버 권한 없음
- 저장 실패
- 재료 부족
- 잘못된 비용 데이터

### 9.5 UI의 예상 사용 절차

1. `GetAllNodeViews()`를 호출해 노드 위젯을 생성한다.
2. `GraphPosition`으로 노드를 배치한다.
3. `PrerequisiteNodeIds`를 사용해 연결선을 그린다.
4. 노드 선택 시 받은 View로 상세 패널을 구성한다.
5. 활성화 버튼에서 `RequestActivateNode(NodeId)`를 호출한다.
6. `MaterialCosts`로 보유량/필요량을 표시하고 `CanActivateNode`로 버튼을 제어한다.
7. `OnNodeActivationResult`, `OnNodeStateChanged`, `OnUpgradeDataChanged`로 화면을 갱신한다.

UI Blueprint는 Data Asset 행 검색, PlayerState 캐스팅 반복, GAS Attribute 직접 접근, RPC 호출, SaveGame 접근을 하지 않아야 한다.

필요하면 `UShipUpgradeBlueprintLibrary::GetLocalShipUpgradeComponent(WorldContext)` 하나를 제공하여 UI가 컴포넌트를 쉽게 얻도록 한다.

---

## 10. 서버 권위 활성화 흐름

```text
UI RequestActivateNode(NodeId)
    → 소유 클라이언트의 UpgradeComponent
    → ServerRequestActivateNode RPC
    → 서버가 NodeId 존재 여부 검증
    → 이미 활성화됐는지 검증
    → 모든 선행 노드(N개) 활성화 여부 검증
    → 비용 데이터 검증 및 같은 ItemTag 합산
    → InventoryComponent의 보유 재료 검증
    → RemoveItemsAtomically로 모든 비용을 한 번에 차감
    → 활성 NodeId에 원자적으로 추가
    → 영구 저장 요청
    → 저장 실패 시 NodeId와 차감 재료 롤백
    → 복제 상태 갱신
    → ClientActivationResult
    → UI 이벤트 발생
```

요청은 멱등적이어야 한다. 같은 NodeId가 네트워크 재전송이나 더블 클릭으로 여러 번 요청되어도 효과가 중복 적용되면 안 된다.

인벤토리 구현은 `ClassFeature`, 강화 구현은 `WaterAndShip`에 있으므로 역방향 모듈 의존성을 만들지 않는다. `WaterAndShip`에 `IShipUpgradeInventoryProvider`라는 좁은 계약을 두고 `UInventoryComponent`가 기존 `GetItemCount`, `RemoveItemsAtomically`, `AddItemsAtomically`를 연결한다. 이 방식으로 Item Crafting API의 검증·차감 규칙을 재사용하면서 모듈 순환 참조를 피한다.

---

## 11. 구현 파일 구성안

```text
Source/WaterAndShip/Public/Upgrade/
    ShipUpgradeTypes.h
    ShipUpgradeTreeDataAsset.h
    ShipUpgradeComponent.h
    ShipUpgradeBlueprintLibrary.h

Source/WaterAndShip/Private/Upgrade/
    ShipUpgradeTreeDataAsset.cpp
    ShipUpgradeComponent.cpp
    ShipUpgradeBlueprintLibrary.cpp

Source/WaterAndShip/Public/
    Ship.h
    ShipPhysicsAsync.h

Source/WaterAndShip/Private/
    Ship.cpp
    ShipPhysicsAsync.cpp
    Cannon.cpp

Source/GASCore/Public/
    ShipAttributeSet.h

Source/GASCore/Private/
    ShipAttributeSet.cpp

Source/ClassFeature/Public/
    BasePlayerState.h

Source/ClassFeature/Private/
    BasePlayerState.cpp
```

저장 기능을 프로젝트 공통 프로필 시스템으로 옮기게 되면 SaveGame 구현 파일은 해당 모듈에 배치하되, `UShipUpgradeComponent`의 공개 UI API는 유지한다.

---

## 12. 단계별 구현 계획

### Phase 1 — 스탯 스키마 정리

- [ ] `FShipStatRow`의 통합 속도 배율을 추진/회전 배율로 분리
- [ ] `UShipAttributeSet`에 두 Attribute와 RepNotify 추가
- [ ] 비동기 물리 입력 및 캐시도 두 배율로 분리
- [ ] 전진 힘과 회전 토크에 각각 다른 배율 적용
- [ ] 기존 `DT_ShipStat` 값 마이그레이션
- [ ] Player와 Enemy 행을 각각 검증

완료 조건:

- WS 배율만 변경했을 때 회전 토크가 바뀌지 않는다.
- AD 배율만 변경했을 때 전진 추진력이 바뀌지 않는다.
- 서버와 클라이언트의 Network Physics 입력이 같은 값을 사용한다.

### Phase 2 — 노드 데이터와 순수 계산기

- [ ] `EShipStatType` 정의
- [ ] Modifier 연산 타입 정의
- [ ] Node Definition과 Tree Data Asset 구현
- [ ] 그래프 데이터 검증 구현
- [ ] 기본값과 활성 노드로 최종 Snapshot을 만드는 순수 계산기 구현
- [ ] 노드 한 개 활성화 미리보기 구현

완료 조건:

- Actor 없이 자동 테스트에서 최종 수치를 계산할 수 있다.
- 같은 입력은 서버와 클라이언트에서 같은 결과를 만든다.
- 잘못된 그래프 자산이 에디터 검증에서 실패한다.

### Phase 3 — 플레이어 강화 상태

- [ ] `UShipUpgradeComponent` 구현
- [ ] `ABasePlayerState`에 기본 컴포넌트 생성
- [ ] 활성 NodeId 복제
- [ ] 서버 활성화 검증과 결과 이벤트 구현
- [ ] 중복 요청의 멱등성 보장

완료 조건:

- 클라이언트가 임의 노드 상태를 직접 변경할 수 없다.
- 선행 노드가 없는 잠긴 노드는 서버에서 거부된다.
- 활성 상태가 Owner 클라이언트 UI에 정상 복제된다.

### Phase 4 — 저장과 맵 이동

- [ ] 활성 NodeId 저장 구조 구현
- [ ] 프로필/SaveGame 로드 후 PlayerState 초기화
- [ ] 맵 이동 후 상태 복원
- [ ] 삭제되거나 이름이 바뀐 NodeId에 대한 경고와 안전한 무시 처리

완료 조건:

- 게임 재실행 또는 요구된 저장 범위에서 활성 노드가 유지된다.
- 전투 맵에 다시 들어가도 같은 강화 결과가 기본값으로 적용된다.

### Phase 5 — 플레이어 Ship 적용

- [ ] 기본 Ship DT 행을 Snapshot으로 변환
- [ ] 플레이어 배 배정 시 UpgradeComponent와 합산
- [ ] 최종 Snapshot을 AttributeSet에 적용
- [ ] 추진/회전 배율을 비동기 물리 입력에 반영
- [ ] 공유 승선 배의 소유/리셋 정책 적용
- [ ] Enemy Ship이 강화 경로를 타지 않는지 검증

완료 조건:

- 강화한 플레이어 배만 수치가 변경된다.
- Enemy 배는 자신의 DT 값으로만 생성된다.
- 하선/재승선/다른 플레이어 승선 시 강화가 잘못 남지 않는다.

### Phase 6 — UI API

- [ ] Node View 생성 함수
- [ ] 현재 상태 및 활성화 가능 여부 함수
- [ ] 현재/예상 Ship Snapshot 함수
- [ ] 완성된 스탯 변경 표시 데이터 제공
- [ ] 활성화 요청과 비동기 결과 이벤트 제공
- [ ] Blueprint Library 접근 함수 제공

완료 조건:

- UI Blueprint가 Data Asset, GAS, SaveGame, RPC에 직접 접근하지 않는다.
- UI 담당자가 공개 함수와 Delegate만으로 전체 노드 UI를 만들 수 있다.

### Phase 7 — 대포 서버 권위 보강

- [ ] `ServerFire`에서 클라이언트 Damage/Speed 입력 제거
- [ ] 서버가 Ship AttributeSet에서 실제 발사 스탯 조회
- [ ] 서버 쿨다운 검증 추가
- [ ] 비정상 반복 발사와 임의 탄속/피해 요청 테스트

완료 조건:

- 클라이언트가 RPC 값을 조작해 강화 범위를 벗어난 피해를 만들 수 없다.
- 실제 발사 결과가 서버의 최종 Ship Snapshot과 일치한다.

---

## 13. 테스트 계획

### 순수 계산 자동 테스트

- 기본값만 있을 때 결과가 DT 값과 같다.
- Flat과 Percent가 정의된 공식대로 합산된다.
- 쿨다운 감소가 최소 0.05초에서 Clamp된다.
- 같은 노드가 중복 입력되어도 한 번만 적용된다.
- 노드 미리보기와 실제 활성화 후 결과가 같다.

### Data Asset 검증 테스트

- 중복 ID 검출
- 없는 선행 노드 검출
- 자기 참조 검출
- 간접 순환 검출
- 빈 Modifier 검출

### Ship 통합 테스트

- WS 추진 배율과 AD 회전 배율 독립성
- 대포 피해, 쿨다운, 탄속 적용
- 최대 체력 및 전투 시작 현재 체력 적용
- PlayerShip과 EnemyShip의 분리

### 네트워크 PIE 테스트

- 서버/클라이언트에서 활성 노드 상태 일치
- Owner UI 이벤트 전달
- 잠긴 노드의 위조 요청 거부
- 연속 더블 클릭 중복 적용 방지
- 서버 발사 쿨다운 우회 방지
- 클라이언트가 임의 Damage/Speed를 주입하지 못함

### 맵 이동/저장 테스트

- 강화 UI 맵에서 노드 활성화
- 전투 맵 진입
- 플레이어 배 최종 스탯 확인
- 비전투 맵 복귀 후 상태 유지
- 전투 맵 재진입 시 동일 강화 재적용

---

## 14. 완료 정의

다음 조건을 모두 만족하면 배 강화 노드 API의 1차 구현이 완료된 것으로 본다.

- 배 스탯이 요구된 여섯 종류로 분리되어 있다.
- 기획자가 코드 수정 없이 노드, 연결 관계, UI 위치, 스탯 변경량을 편집할 수 있다.
- 활성화 상태가 영구 저장되고 전투 맵 진입 시 플레이어 배 기본값에 반영된다.
- Enemy 배는 플레이어 강화의 영향을 받지 않는다.
- 서버가 활성화와 실제 전투 수치를 권위 있게 결정한다.
- UI 담당자가 공개된 Blueprint 함수와 Delegate만으로 강화 UI를 구현할 수 있다.
- 자동 테스트와 네트워크 테스트가 계산, 저장, 적용, 보안 경계를 검증한다.

---

## 15. 핵심 설계 원칙

```text
Ship Data Table = 배 종류별 기본값
Upgrade Tree Data Asset = 기획자가 만든 강화 규칙
Player Upgrade State = 활성화된 NodeId 목록
Ship AttributeSet = 기본값과 활성 노드를 계산한 실제 전투 수치
UI API = 위 구현 세부사항을 숨긴 View와 Request 계층
```

UI, 저장, 네트워크, Ship 물리가 각자 별도의 계산 규칙을 가지지 않도록 한다. 모든 화면 미리보기와 실제 전투 적용은 동일한 순수 스탯 계산기를 사용해야 한다.
