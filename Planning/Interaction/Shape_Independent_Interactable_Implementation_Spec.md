# Shape-Independent Interactable 구현 명세서

> 문서 상태: 구현 전 확정 명세  
> 기준 코드: `c3599ba3` (`KKH/Temp`, 2026-09-16 확인)  
> 대상 엔진: Unreal Engine 5.7  
> 구현 Agent는 이 문서에 명시되지 않은 구조 변경, 자산 마이그레이션, 테스트 실행을 임의로 수행하지 않는다.

## 1. 문서 목적

현재 `UInteractableComponent`는 `USphereComponent`를 직접 상속한다. 이 때문에 모든 상호작용 영역이 Sphere로 제한된다.

본 작업의 목적은 기존 Sphere 기반 상호작용 Actor를 변경하거나 재설정하지 않으면서, 신규 상호작용 지점에서 Box와 Capsule 같은 Unreal 기본 충돌 도형도 동일한 상호작용 시스템에 사용할 수 있게 만드는 것이다.

이 문서는 구현 명세이며, 이 문서 작성 작업 자체에서는 C++ 또는 Blueprint 자산을 변경하지 않는다.

### 1.1 핵심 mental model

상호작용 시스템은 두 개의 서로 다른 Shape를 사용한다.

1. 플레이어의 탐색 Shape: `ABasePlayer::PerformInteractTrace()`가 이동시키는 작은 sphere sweep
2. 대상의 수신 Shape: Actor에 부착되어 `ECC_Interactable` query를 받는 primitive component

이번 작업에서 바꾸는 것은 2번뿐이다. 1번 sweep은 계속 sphere이며, 대상이 Sphere/Box/Capsule 중 무엇인지는 sweep 결과의 `FHitResult::GetComponent()`가 결정한다. 따라서 Shape 독립성은 탐색 알고리즘을 일반화하는 문제가 아니라, Hit component가 동일한 `IInteractable` 계약을 구현하도록 만드는 문제다.

### 1.2 현재 코드 기준 사실

- `IInteractable` 구현체는 현재 `UInteractableComponent` 하나뿐이다.
- `UInteractableComponent`는 `USphereComponent`를 직접 상속한다.
- 실제 능력 실행 경로는 이미 `Cast<IInteractable>(Hit.GetComponent())`를 사용한다.
- UI 스캔 경로만 `FindComponentByClass<UInteractableComponent>()`로 Sphere 구체 타입을 재조회한다.
- `Interactable` collision profile은 `QueryOnly`이고 `ECC_Interactable` trace에 block 응답한다.
- `AShipBoardingPoint`는 `BoardingInteractable` 하나를 소유하며 `OnConstruction()`에서 legacy radius/transform 프로퍼티를 매번 컴포넌트에 다시 쓴다.
- `AShipBoardingPoint::BeginPlay()`는 선박 정책을 검사한 뒤 UI 문구와 delegate를 초기화한다.
- `BP_PlayerShip_Kelvin.uasset`과 `BP_PlayerShip.uasset`은 존재하지만, 본 명세 작성 시 `.uasset` 내부의 인스턴스별 legacy 값은 확인하지 않았다. 해당 값은 마이그레이션 전에 에디터에서 반드시 기록해야 한다.

---

## 2. 변경 범위

### 2.1 변경 대상

- 상호작용 컴포넌트가 Sphere 이외의 기본 Shape도 지원하도록 확장
- 신규 Box/Capsule 상호작용 컴포넌트 제공
- 모든 상호작용 Shape가 기존 `IInteractable` 계약을 동일하게 구현
- UI 조회에 남아 있는 `UInteractableComponent` 구체 타입 의존 제거
- `AShipBoardingPoint`에서 Sphere/Box/Capsule 중 하나를 선택 가능하게 구성
- 선택한 Shape를 Blueprint Viewport에서 직접 확인하고 Transform/Scale을 편집 가능하게 구성

### 2.2 명시적 비변경 대상

다음 동작은 현재 구현을 유지한다.

- `ECC_Interactable` 기반 `SweepMultiByChannel`
- 플레이어 상호작용 거리와 반경 계산
- Hit 결과 중 가장 가까운 상호작용 대상 선택
- Gameplay Ability TargetData 구성과 전송
- 서버 `ProcessInteract()` 처리 순서
- 서버의 `IInteractable::Interact()` 호출
- `InteractionTag` 기반 Gameplay Event 발송
- 기존 `OnInteracted` delegate 및 Actor callback 실행
- 기존 네트워크 권한 구조
- 기존 상호작용 Actor의 Blueprint 설정값

본 작업은 탐지·검증·실행 파이프라인의 재설계가 아니다. 리팩토링 범위는 충돌 도형에 대한 Sphere 종속성 제거로 제한한다.

---

## 3. 호환성 최우선 원칙

기존 클래스는 다음 상속 구조를 그대로 유지한다.

```cpp
UInteractableComponent : public USphereComponent, public IInteractable
```

다음 항목을 변경하지 않는다.

- 클래스 이름 `UInteractableComponent`
- `USphereComponent` 상속
- 기존 UPROPERTY 이름과 소유 클래스
- 기존 Blueprint component template
- 기존 Sphere Radius와 Relative Transform
- 기존 C++ getter 및 멤버 타입
- 기존 `CreateDefaultSubobject<UInteractableComponent>()` 호출부

따라서 다음 기존 사용처에는 Shape 마이그레이션을 요구하지 않는다.

- 아이템과 무기
- NPC 대화
- 보관함
- 대포
- 함선 조타 장치
- 닻
- 기타 기존 `UInteractableComponent` 기반 Actor

기존 Actor는 작업 전과 동일한 Sphere 컴포넌트, 충돌 설정, Viewport 표시 및 런타임 동작을 유지해야 한다.

---

## 4. Unreal 상속 제약과 최종 구조

`USphereComponent`, `UBoxComponent`, `UCapsuleComponent`는 서로 다른 `UShapeComponent` 파생 클래스다. Unreal의 UObject/UCLASS는 이들 구현 클래스를 다중 상속할 수 없다.

따라서 하나의 범용 UCLASS가 세 기본 Shape의 물리 구현을 교체하는 구조는 사용하지 않는다. 엔진 기본 Shape의 충돌 생성, Bounds 계산 및 Viewport visualization을 그대로 사용하기 위해 Shape별 컴포넌트를 제공한다.

```text
IInteractable
├─ UInteractableComponent        : USphereComponent
├─ UBoxInteractableComponent     : UBoxComponent
└─ UCapsuleInteractableComponent : UCapsuleComponent
```

공통 부모 역할은 `IInteractable`이 담당한다. 실제 충돌과 에디터 시각화는 각 Unreal 기본 Shape 클래스가 담당한다.

이 구조의 결과는 다음과 같다.

- 기존 Sphere 컴포넌트는 완전히 보존된다.
- 신규 Box/Capsule 컴포넌트도 Hit component 자체가 `IInteractable`이다.
- 기존 GA와 서버 코드는 계속 `Cast<IInteractable>(Hit.GetComponent())`를 사용할 수 있다.
- Hit component에서 Attach Parent 또는 Owner를 다시 검색하는 신규 탐지 체계가 필요 없다.
- 각 Shape는 Blueprint Viewport에서 Unreal 기본 wireframe으로 표시된다.

---

## 5. `IInteractable` 계약 변경

### 5.1 기존 계약 유지

다음 함수는 시그니처와 의미를 변경하지 않는다.

```cpp
virtual FGameplayTag GetInteractionTag() const = 0;
virtual void Interact(AActor* Interactor) = 0;
```

### 5.2 UI 정보 접근 함수 추가

현재 실제 상호작용 선택과 서버 처리는 `IInteractable`에 의존하지만, `ABasePlayer::PerformInteractionScan()`의 UI 갱신은 Actor에서 `UInteractableComponent`를 다시 찾는다.

```cpp
HitActor->FindComponentByClass<UInteractableComponent>()
```

이 구체 타입 의존만 제거하기 위해 다음 읽기 전용 함수를 인터페이스에 추가한다.

```cpp
virtual const FInteractionUIInfo& GetInteractionUIInfo() const = 0;
```

각 Shape 컴포넌트는 자신이 보유한 `InteractUIInfo`를 반환한다.

`Interactable.h`에서는 순환 include를 만들지 않도록 구조체를 전방 선언한다.

```cpp
struct FInteractionUIInfo;

class ARTISTICSWCORE_API IInteractable
{
	GENERATED_BODY()

public:
	virtual FGameplayTag GetInteractionTag() const = 0;
	virtual const FInteractionUIInfo& GetInteractionUIInfo() const = 0;
	virtual void Interact(AActor* Interactor) = 0;
};
```

`GetInteractionUIInfo()`는 다음 규칙을 지킨다.

- `const` reference를 반환한다. 임시 객체를 만들거나 복사본의 reference를 반환하지 않는다.
- 반환 대상은 해당 컴포넌트의 `UPROPERTY FInteractionUIInfo InteractUIInfo`다.
- 호출자는 reference를 프레임 또는 함수 호출 범위를 넘어 저장하지 않는다.
- Blueprint 함수로 노출하지 않는다. 현재 호출자는 C++ UI scan뿐이다.

### 5.3 delegate 선언 위치

세 컴포넌트가 같은 delegate 타입을 사용해야 하므로 `FOnInteractedSignature` 선언을 `InteractableComponent.h`에서 `Interactable.h`로 이동한다.

```cpp
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FOnInteractedSignature,
	AActor*,
	Interactor);
```

규칙:

- 타입 이름 `FOnInteractedSignature`를 바꾸지 않는다.
- 기존 `UInteractableComponent::OnInteracted` 프로퍼티 이름과 타입을 바꾸지 않는다.
- 신규 Box/Capsule도 동일 타입의 `OnInteracted`를 선언한다.
- 별도의 Shape별 delegate 타입을 만들지 않는다.
- 이 이동은 같은 Runtime module 내부의 C++ 선언 위치만 바꾸며, Core Redirect를 추가하지 않는다.

이 변경은 sweep, 대상 선택 또는 서버 검증 방식의 변경이 아니다. 이미 Hit된 상호작용 컴포넌트에서 UI 데이터를 읽도록 구체 타입 의존만 제거하는 것이다.

---

## 6. Shape별 컴포넌트 명세

### 6.1 기존 Sphere

파일:

- `Source/ArtisticSWCore/Public/Interactable/InteractableComponent.h`
- `Source/ArtisticSWCore/Private/Interactable/InteractableComponent.cpp`

변경 내용:

- 기존 상속과 프로퍼티를 유지한다.
- `GetInteractionUIInfo()` override만 추가한다.
- 기존 Tick 및 debug sphere 동작을 유지한다.
- 생성자 기본 반경 `100.0f`, collision profile `Interactable`, tick interval `0.1f`를 변경하지 않는다.
- 기존 `Interact()`의 null interactor 무시 규칙을 변경하지 않는다.

### 6.2 신규 Box

신규 파일:

- `Source/ArtisticSWCore/Public/Interactable/BoxInteractableComponent.h`
- `Source/ArtisticSWCore/Private/Interactable/BoxInteractableComponent.cpp`

클래스:

```cpp
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class ARTISTICSWCORE_API UBoxInteractableComponent
    : public UBoxComponent
    , public IInteractable
```

필수 기능:

- 기본 collision profile은 `Interactable`
- `InteractionTag`
- `InteractUIInfo`
- `InteractPopupUIClass`
- `OnInteracted`
- `InitializeInteractable()`
- `GetInteractionTag()`
- `GetInteractionUIInfo()`
- `Interact()`
- 선택적 런타임 debug box 표시

확정 기본값과 구현 규칙:

```text
BoxExtent                   = (100, 100, 100) cm
CollisionProfile           = Interactable
PrimaryComponentTick       = enabled
TickInterval               = 0.1 s
bDrawDebugInteractionRange = false
DebugColor                 = Cyan
DebugLifetime              = 0.12 s
```

- 생성자에서는 `InitBoxExtent(FVector(100.0f))`를 사용한다.
- debug box는 `GetComponentLocation()`, `GetScaledBoxExtent()`, `GetComponentQuat()`을 사용한다.
- `Interact()`는 `Interactor != nullptr`일 때만 `OnInteracted.Broadcast(Interactor)`를 호출한다.
- `InitializeInteractable()`은 `ObjectName`과 `ActionText`만 대입한다.
- `InteractionTag`, `InteractUIInfo`, `InteractPopupUIClass`, `OnInteracted`의 UPROPERTY specifier와 category는 기존 Sphere와 동일하게 유지한다.

### 6.3 신규 Capsule

신규 파일:

- `Source/ArtisticSWCore/Public/Interactable/CapsuleInteractableComponent.h`
- `Source/ArtisticSWCore/Private/Interactable/CapsuleInteractableComponent.cpp`

클래스:

```cpp
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class ARTISTICSWCORE_API UCapsuleInteractableComponent
    : public UCapsuleComponent
    , public IInteractable
```

필수 기능은 Box 컴포넌트와 동일하며 debug 표현만 capsule을 사용한다.

확정 기본값과 구현 규칙:

```text
CapsuleRadius               = 100 cm
CapsuleHalfHeight           = 100 cm
CollisionProfile           = Interactable
PrimaryComponentTick       = enabled
TickInterval               = 0.1 s
bDrawDebugInteractionRange = false
DebugColor                 = Cyan
DebugLifetime              = 0.12 s
```

- 생성자에서는 `InitCapsuleSize(100.0f, 100.0f)`를 사용한다.
- Unreal invariant인 `HalfHeight >= Radius`를 깨는 기본값을 사용하지 않는다.
- debug capsule은 `GetScaledCapsuleHalfHeight()`, `GetScaledCapsuleRadius()`, `GetComponentQuat()`을 사용한다.
- 초기화, interface 함수 및 delegate broadcast 규칙은 Box와 동일하다.

### 6.4 프로퍼티 호환 규칙

기존 `UInteractableComponent`의 UPROPERTY를 공통 구조체로 이동하지 않는다. 기존 Blueprint 직렬화 경로가 변경될 수 있기 때문이다.

Box와 Capsule은 동일한 이름과 카테고리의 프로퍼티를 각각 선언한다. 짧은 공통 함수 본문은 private C++ utility로 공유할 수 있지만 기존 Sphere 프로퍼티의 소유권과 경로는 유지한다.

본 변경에서는 helper base UObject/UActorComponent, template component base, data asset, 신규 subsystem을 만들지 않는다. 세 엔진 Shape는 서로 다른 reflected base class를 필요로 하며, 소량의 중복이 Blueprint 직렬화 안정성보다 우선한다.

### 6.5 include 및 UHT 규칙

- 각 신규 header는 자기 엔진 base header를 직접 include한다.
  - Box: `Components/BoxComponent.h`
  - Capsule: `Components/CapsuleComponent.h`
- `Interactable.h`와 `InteractUserWidget.h`를 직접 include한다.
- `UUserWidget`은 전방 선언한다.
- 각 `.generated.h`는 해당 header의 마지막 include다.
- `ArtisticSWCore.Build.cs`에는 신규 module dependency가 필요하지 않다. `Engine`, `UMG`, `GameplayTags`가 이미 존재한다.
- `PublicIncludePaths`는 이미 `ArtisticSWCore/Public/Interactable`을 포함하므로 수정하지 않는다.

---

## 7. Viewport 시각화 및 편집 명세

세 컴포넌트는 모두 Unreal 기본 `UShapeComponent` 파생 클래스이므로 엔진의 기본 에디터 visualization을 사용한다.

개발자는 Blueprint Viewport에서 다음 작업을 수행할 수 있어야 한다.

- 상호작용 Shape 선택
- wireframe 영역 확인
- 위치 이동
- 회전
- Scale 조절
- Sphere Radius 편집
- Box Extent 편집
- Capsule Radius 및 Half Height 편집

별도 Static Mesh 또는 editor-only visualization Actor를 만들지 않는다.

선택된 ShipBoardingPoint Shape에는 식별하기 쉬운 Shape Color를 적용한다. 게임 렌더링용 메시를 추가하지 않는다.

Shape color는 세 컴포넌트 constructor에서 `ShapeColor = FColor::Cyan;`으로 설정한다. Shape subclass 내부의 inherited member를 직접 설정하며 editor module dependency를 추가하지 않는다.

Shape Scale은 Unreal 기본 Shape scaling 규칙을 따른다.

- Box는 축별 Scale을 사용할 수 있다.
- Sphere는 가장 큰 축을 기준으로 충돌 반경이 스케일된다.
- Capsule은 엔진의 반경/높이 스케일 규칙을 따른다.
- 정확한 Sphere/Capsule 치수는 비균일 Scale보다 Radius/Half Height 속성으로 조절한다.

비선택 Shape는 `SetCollisionEnabled(NoCollision)` 및 `SetVisibility(false)` 상태다. 선택 Shape는 `SetVisibility(true)` 상태다. `SetHiddenInGame()`만으로 editor wireframe visibility를 제어하지 않는다. 선택을 바꾸면 `OnConstruction()`이 새 선택 Shape의 visibility를 갱신한다.

컴포넌트의 `RelativeLocation`, `RelativeRotation`, `RelativeScale3D`, Sphere Radius, Box Extent, Capsule Radius/Half Height는 각 component template에서 authoring한다. 단, legacy Sphere 호환 모드가 켜진 자산은 9.7의 예외 규칙을 따른다.

---

## 8. 플레이어 UI 스캔의 최소 수정

파일:

- `Source/ClassFeature/Private/BasePlayer.cpp`

현재 `PerformInteractionScan()`은 Hit Actor에서 Sphere 전용 컴포넌트를 다시 찾는다. 이를 실제 Hit component의 인터페이스로 교체한다. 단순히 첫 번째 `FindComponentByClass`만 바꾸면 두 번째 UI 갱신 지점에서 어떤 Hit component의 UI 정보인지 잃으므로, 한 번의 scan 동안 widget과 UI 정보의 대응을 함께 보존해야 한다.

변경 전 개념:

```cpp
UInteractableComponent* InteractComp =
    HitActor->FindComponentByClass<UInteractableComponent>();
```

변경 후 개념:

```cpp
IInteractable* Interactable =
    Cast<IInteractable>(Hit.GetComponent());
```

UI 갱신은 다음 데이터를 사용한다.

```cpp
Interactable->GetInteractionUIInfo()
```

### 8.1 확정 scan 알고리즘

`PerformInteractionScan()` 내부에 함수-local map을 추가한다.

```cpp
TArray<UWidgetComponent*> CurrentHoveredWidgets;
TMap<UWidgetComponent*, FInteractionUIInfo> CurrentWidgetUIInfo;
```

각 `FHitResult` 처리 순서는 다음과 같다.

1. `Hit.GetComponent()`를 null 검사한다.
2. `Cast<IInteractable>(Hit.GetComponent())`가 실패하면 해당 Hit를 건너뛴다.
3. `Hit.GetActor()`가 null이면 건너뛴다.
4. Hit Actor의 `UWidgetComponent`를 조회한다.
5. `UInteractUserWidget`을 실제로 소유한 widget만 대상으로 한다.
6. `CurrentHoveredWidgets.AddUnique(WidgetComp)`를 수행한다.
7. `CurrentWidgetUIInfo.Contains(WidgetComp)`가 false일 때만 `Add(WidgetComp, Interactable->GetInteractionUIInfo())`로 복사본을 추가한다.

동일 widget이 한 scan에서 둘 이상의 interactable component를 통해 발견되면 `HitResults` 배열에서 먼저 발견된 유효 component의 UI 정보를 사용한다. 나중 Hit가 먼저 저장된 값을 덮어쓰지 않는다. 현재 `AShipBoardingPoint`에서는 선택된 Shape 하나만 collision을 가지므로 이 규칙이 모호성을 만들지 않는다.

기존 cache 제거/숨김 로직은 유지한다. 신규 widget을 표시하는 블록에서는 owner Actor에서 component를 다시 찾지 않고 `CurrentWidgetUIInfo.Find(Widget)` 결과를 사용한다. map entry가 없으면 widget 표시와 cache 등록은 수행하되 `OnUpdateInteractUI()`는 호출하지 않는다. 정상 경로에서는 entry가 반드시 존재한다.

기존 동작과 동일하게 `OnUpdateInteractUI()`는 widget이 cache에 처음 추가될 때만 호출한다. 매 scan tick마다 Blueprint event를 호출하도록 동작을 확장하지 않는다.

### 8.2 include 정리

- `BasePlayer.cpp`는 interface cast 때문에 `Interactable.h`를 계속 include한다.
- 이 함수 외 다른 사용처가 없다면 `InteractableComponent.h` include는 제거한다. 제거 전 파일 전체에서 구체 타입 사용이 없는지 `rg`로 재확인한다.
- `InteractUserWidget.h`와 `Components/WidgetComponent.h` include는 유지한다.

다음 코드는 변경하지 않는다.

- `PerformInteractTrace()`의 sweep 입력
- `ECC_Interactable`
- trace distance와 radius
- `UInteract::ActivateAbility()`의 대상 거리 비교
- `UInteract::ProcessInteract()`의 서버 실행 흐름
- `CachedHoveredWidgets`의 타입과 lifetime
- 한 scan에 감지된 모든 Actor의 interaction widget을 표시하는 현재 정책
- UI cache가 갱신되는 시점

---

## 9. `AShipBoardingPoint` 구현 명세

### 9.1 컴포넌트 구성

기존 Sphere 컴포넌트 이름과 포인터는 호환을 위해 유지한다.

```text
AShipBoardingPoint
├─ SceneRoot
├─ PointMesh
├─ BoardingInteractable        : UInteractableComponent
├─ BoardingBoxInteractable     : UBoxInteractableComponent
└─ BoardingCapsuleInteractable : UCapsuleInteractableComponent
```

기존 `GetBoardingInteractable()` getter는 이름, 반환 타입, 반환값을 변경하지 않으며 항상 legacy Sphere 포인터를 반환한다. 신규 generic 접근이 필요한 검사 코드를 위해 다음 getter를 추가한다.

```cpp
UFUNCTION(BlueprintPure, Category = "Ship|Boarding")
UShapeComponent* GetActiveBoardingInteractable() const;
```

이 함수는 `InteractionShape`에 따라 세 포인터 중 하나를 반환하며, 예상하지 못한 enum 값에는 Sphere를 반환한다. 기존 getter를 generic getter로 바꾸지 않는다.

constructor의 default subobject 이름은 직렬화 식별자이므로 다음 문자열을 정확히 사용한다.

```cpp
BoardingInteractable = CreateDefaultSubobject<UInteractableComponent>(
	TEXT("BoardingInteractable"));
BoardingBoxInteractable = CreateDefaultSubobject<UBoxInteractableComponent>(
	TEXT("BoardingBoxInteractable"));
BoardingCapsuleInteractable = CreateDefaultSubobject<UCapsuleInteractableComponent>(
	TEXT("BoardingCapsuleInteractable"));
```

세 컴포넌트 모두 `SceneRoot`에 직접 `SetupAttachment()`한다. constructor 종료 시 Sphere는 기본 선택이므로 visible/query-enabled 상태를 유지하고, Box/Capsule은 `NoCollision`, invisible 상태로 시작한다. 최종 상태는 `OnConstruction()`에서 다시 확정한다.

### 9.2 Shape 선택

다음 enum을 추가한다.

```cpp
UENUM(BlueprintType)
enum class EBoardingInteractionShape : uint8
{
	Sphere,
	Box,
	Capsule
};
```

기본값은 기존 동작 보존을 위해 `Sphere`다.

```cpp
UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ship|Boarding|Interaction")
EBoardingInteractionShape InteractionShape = EBoardingInteractionShape::Sphere;
```

이 프로퍼티는 authoring-time 선택이다. 런타임 Blueprint에서 값을 바꾸는 기능은 제공하지 않는다. 런타임 Shape 전환 API, replication, `OnRep`은 구현하지 않는다.

### 9.3 활성 Shape 규칙

`OnConstruction()`과 `BeginPlay()`에서 공통 private 함수 `RefreshInteractionShapeState()`를 호출한다. 선택된 Shape만 상호작용 query에 참여한다.

| Shape 상태 | Collision Enabled | Viewport visualization |
|---|---|---|
| 선택됨 | `QueryOnly` | 표시 |
| 선택되지 않음 | `NoCollision` | 숨김 |

Ship class policy인 `AllowsPlayerBoarding()`이 false이면 선택 Shape도 `NoCollision`이어야 한다.

`RefreshInteractionShapeState()`의 정확한 판정은 다음과 같다.

```text
bAllowsBoarding = (GetOwningShip() == nullptr) || GetOwningShip()->AllowsPlayerBoarding()
bSelected       = component가 InteractionShape와 일치
bEnableQuery    = bSelected && bAllowsBoarding
```

각 Shape에 대해:

```text
CollisionEnabled = bEnableQuery ? QueryOnly : NoCollision
Visibility       = bSelected
HiddenInGame     = true
```

`HiddenInGame = true`는 runtime 렌더링을 막기 위한 값이며, editor component visualization은 `Visibility`로 선택 Shape만 노출한다. engine 기본 shape visualization 동작상 `HiddenInGame`이 editor wireframe까지 숨기는 버전 차이가 확인되면, runtime game world에서만 `SetHiddenInGame(true)`를 호출한다. 이를 해결하기 위해 별도 mesh를 만들지 않는다.

모든 Shape는 constructor에서 collision profile `Interactable`을 가진다. 비선택 Shape를 profile 변경으로 비활성화하지 말고 `CollisionEnabled`만 `NoCollision`로 바꾼다. 그래야 선택 전환 시 profile을 재구축할 필요가 없다.

### 9.4 초기화와 delegate

세 컴포넌트에 동일한 텍스트와 callback을 설정한다.

```text
Object: Ship
Action: Board
Callback: AShipBoardingPoint::HandleInteracted
```

선택되지 않은 Shape는 collision이 비활성화되어 있으므로 delegate가 바인딩되어 있어도 Hit되지 않는다.

`BeginPlay()`는 선박이 boarding을 허용하지 않아도 조기 return하지 않는다. 세 Shape 모두 다음 초기화를 먼저 수행한다.

1. `InitializeInteractable(Ship, Board)`
2. `OnInteracted.AddUniqueDynamic(this, &AShipBoardingPoint::HandleInteracted)`
3. `RefreshInteractionShapeState()`

선박 정책이 false이면 3번에서 모든 collision이 꺼진다. 초기화를 생략하는 조기 return은 제거한다. 이 순서는 추후 같은 인스턴스가 재활성화될 때 미초기화 상태가 되는 것을 방지하지만, 이번 작업에서 동적 정책 갱신 API는 추가하지 않는다.

`HandleInteracted()`와 `AShip::BoardFromSea()`는 변경하지 않는다.

### 9.5 Viewport authoring

세 Shape 포인터는 `VisibleAnywhere, BlueprintReadOnly` component property로 노출한다. Blueprint Components 트리와 Viewport에서 선택할 수 있어야 한다.

개발자는 다음 순서로 편집한다.

1. `InteractionShape` 선택
2. 선택된 Shape component 선택
3. Viewport에서 위치와 회전 조절
4. Shape 고유 크기 또는 Component Scale 조절
5. wireframe으로 실제 영역 확인

### 9.6 기존 BoardingPoint 값 마이그레이션

현재 값:

- `InteractionSphereRadius`
- `InteractionRelativeTransform`

기존 `BP_PlayerShip_Kelvin` Child Actor의 모양과 위치를 보존하기 위해 Sphere component로 값을 이전해야 한다. `.uasset`의 실제 override 값은 본 명세 작성 시 확인되지 않았으므로 C++ 구현 단계에서 추측하거나 기본값으로 덮어쓰지 않는다.

즉시 legacy 프로퍼티를 deprecated하고 `OnConstruction()` 대입을 제거하면 기존 Blueprint 인스턴스의 값이 사라질 수 있다. 따라서 이번 변경에는 명시적 호환 프로퍼티를 추가한다.

```cpp
UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ship|Boarding|Interaction|Legacy")
bool bUseLegacySphereAuthoring = true;
```

규칙:

- 기본값은 `true`다. 기존 자산과 신규 CDO 모두 안전한 기존 동작으로 시작한다.
- `true`이고 `InteractionShape == Sphere`일 때만 `OnConstruction()`이 기존 `InteractionSphereRadius`와 `InteractionRelativeTransform`을 `BoardingInteractable`에 적용한다.
- Box/Capsule 선택에는 legacy 값이 절대 적용되지 않는다.
- `false`이면 `OnConstruction()`은 Sphere의 radius 또는 relative transform을 쓰지 않는다. component template에 저장된 값을 그대로 사용한다.
- 이번 C++ 변경에서 `InteractionSphereRadius`와 `InteractionRelativeTransform`에 `DeprecatedProperty` metadata를 붙이지 않는다. 마이그레이션 전 asset compatibility 경로로 실제 사용되기 때문이다.
- 기존 두 프로퍼티의 이름, 타입, 기본값, category는 변경하지 않는다.

### 9.7 에디터 자산 마이그레이션 게이트

구현 Agent가 C++ 컴파일을 완료한 후, 사용자가 Unreal Editor에서 다음을 수동 수행한다. 프로젝트 지침상 구현 Agent는 editor/headless editor를 실행하거나 `.uasset`을 임의 수정하지 않는다.

각 `BP_PlayerShip_Kelvin` 내부 ShipBoardingPoint Child Actor instance에 대해:

1. 현재 `InteractionSphereRadius` 값을 기록한다.
2. 현재 `InteractionRelativeTransform`의 Location/Rotation/Scale을 기록한다.
3. `BoardingInteractable` component의 Sphere Radius와 Relative Transform에 동일 값을 입력한다.
4. `bUseLegacySphereAuthoring`을 `false`로 설정한다.
5. Blueprint를 Compile/Save한다.
6. Viewport에서 전환 전후 wireframe의 위치와 크기가 동일한지 확인한다.
7. 두 Child Actor instance가 모두 존재한다면 각각 독립적으로 반복한다. 개수를 추측하지 말고 Components/Details에서 실제 개수를 확인한다.

마이그레이션이 완료되고 모든 관련 Blueprint가 저장되기 전에는 legacy 프로퍼티 삭제 또는 deprecation 후속 작업을 수행하지 않는다. 후속 cleanup은 별도 작업이다.

### 9.8 header와 private helper

`ShipBoardingPoint.h`에 다음 전방 선언을 추가한다.

```cpp
class UBoxInteractableComponent;
class UCapsuleInteractableComponent;
class UShapeComponent;
```

추가 component 포인터는 기존 Sphere와 동일하게 `VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Boarding"` 및 `TObjectPtr`를 사용한다.

```cpp
UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Boarding")
TObjectPtr<UBoxInteractableComponent> BoardingBoxInteractable;

UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Boarding")
TObjectPtr<UCapsuleInteractableComponent> BoardingCapsuleInteractable;
```

private 또는 protected helper는 다음 책임만 가진다.

```cpp
void InitializeInteractionShapeComponents();
void RefreshInteractionShapeState();
```

- 첫 helper는 `BeginPlay()`에서 세 컴포넌트의 UI/delegate를 초기화한다.
- 둘째 helper는 selection, visibility, boarding policy, collision enabled를 적용한다.
- helper는 component를 생성하거나 destroy하지 않는다.
- component 생성은 constructor의 `CreateDefaultSubobject`에서만 한다.

---

## 10. 구현 순서

1. 변경 전 `git status --short`를 기록하고 사용자 변경을 보존한다.
2. `Interactable.h`에 `FInteractionUIInfo` 전방 선언, 공통 delegate 선언, `GetInteractionUIInfo()`를 추가한다.
3. `InteractableComponent.h`에서 delegate 중복 선언을 제거하고 UI getter override를 선언한다.
4. `InteractableComponent.cpp`에 UI getter 정의를 추가한다.
5. `UBoxInteractableComponent` header/source를 추가한다.
6. `UCapsuleInteractableComponent` header/source를 추가한다.
7. `BasePlayer.cpp::PerformInteractionScan()`을 8.1 알고리즘대로 수정한다.
8. `ShipBoardingPoint.h`에 enum, 신규 포인터, active getter, legacy mode, helper 선언을 추가한다.
9. `ShipBoardingPoint.cpp` constructor에서 Box/Capsule default subobject를 생성하고 attachment/profile/default visibility를 설정한다.
10. `OnConstruction()`에 legacy Sphere 조건부 적용과 `RefreshInteractionShapeState()` 호출을 구현한다.
11. `BeginPlay()`를 세 Shape 공통 초기화 후 상태 refresh 순서로 수정한다.
12. 정적 검색으로 모든 `IInteractable` 구현체가 신규 pure virtual 함수를 구현했는지 확인한다.
13. 정적 검색으로 `PerformInteractionScan()`에 Sphere 구체 타입 재조회가 남지 않았는지 확인한다.
14. 프로젝트 지침에 따라 Editor target을 한 번 컴파일한다.
15. 컴파일 성공 후 C++ 구현을 종료하고 9.7의 수동 asset migration checklist를 사용자에게 전달한다.

### 10.1 파일별 변경 표

| 파일 | 조치 | 금지 사항 |
|---|---|---|
| `Source/ArtisticSWCore/Public/Interactable/Interactable.h` | UI getter와 공통 delegate | 기존 함수 signature 변경 금지 |
| `Source/ArtisticSWCore/Public/Interactable/InteractableComponent.h` | getter override, delegate 선언 제거 | 클래스명/base/UPROPERTY rename 금지 |
| `Source/ArtisticSWCore/Private/Interactable/InteractableComponent.cpp` | getter 정의 | 기존 constructor/tick/interact 의미 변경 금지 |
| `.../Public/Interactable/BoxInteractableComponent.h` | 신규 | 공통 UObject base 도입 금지 |
| `.../Private/Interactable/BoxInteractableComponent.cpp` | 신규 | custom collision primitive 금지 |
| `.../Public/Interactable/CapsuleInteractableComponent.h` | 신규 | 공통 UObject base 도입 금지 |
| `.../Private/Interactable/CapsuleInteractableComponent.cpp` | 신규 | custom collision primitive 금지 |
| `Source/ClassFeature/Private/BasePlayer.cpp` | Hit interface 기반 UI lookup | sweep/거리/GA 변경 금지 |
| `Source/WaterAndShip/Public/ShipBoardingPoint.h` | enum, components, compatibility flag/getter/helpers | 기존 getter 제거 금지 |
| `Source/WaterAndShip/Private/ShipBoardingPoint.cpp` | 생성/초기화/state refresh | `HandleInteracted` 변경 금지 |
| Build.cs 및 Config | 변경 없음 | 신규 module/profile/channel 추가 금지 |

### 10.2 구현 후 정적 검사 명령

PowerShell workspace root에서 다음 검색을 수행한다. 생성 파일과 build output은 검색하지 않는다.

```powershell
rg -n --glob '*.h' --glob '!**/*.generated.h' "public\s+IInteractable" Source
rg -n "GetInteractionUIInfo" Source/ArtisticSWCore Source/ClassFeature
rg -n "FindComponentByClass<UInteractableComponent>" Source/ClassFeature/Private/BasePlayer.cpp
rg -n "Boarding(Box|Capsule)?Interactable|bUseLegacySphereAuthoring" Source/WaterAndShip
```

세 번째 검색 결과는 0건이어야 한다. 다른 파일의 Sphere 구체 타입 사용은 이번 범위에서 일괄 변경하지 않는다.

### 10.3 컴파일 규칙

- target: `ArtisticSW2026Editor`
- platform/configuration: `Win64 Development`
- 현재 설치된 UE 5.7의 UnrealBuildTool 또는 project solution build 경로를 사용한다.
- 정확한 Engine 설치 경로를 추측해 문서에 하드코딩하지 않는다.
- 출력은 프로젝트 지침대로 앞부분 40줄 및 4000 bytes 이하로 제한한다.
- 실패 시 상위 3개 compile error만 분석하고 중단한다.
- automation test, commandlet, headless editor, PIE, standalone binary를 실행하지 않는다.

---

## 11. 완료 조건

### 11.1 기존 시스템

- 기존 `UInteractableComponent`가 계속 `USphereComponent`를 상속한다.
- 기존 Blueprint가 컴포넌트 교체 없이 로드된다.
- 기존 Sphere Radius와 Transform이 유지된다.
- 기존 Actor의 상호작용 탐지와 실행 결과가 변경되지 않는다.
- 기존 `OnInteracted` callback과 Gameplay Event가 동일하게 실행된다.
- 기존 `FOnInteractedSignature` 타입명과 `OnInteracted` 프로퍼티명이 유지된다.
- `InteractPopupUIClass`를 사용하는 기존 crafting 경로는 변경되지 않는다.

### 11.2 신규 Shape

- Box 컴포넌트가 기존 sweep에 직접 Hit된다.
- Capsule 컴포넌트가 기존 sweep에 직접 Hit된다.
- 두 컴포넌트 모두 기존 `IInteractable::Interact()` 경로로 실행된다.
- UI가 Sphere/Box/Capsule에서 동일하게 표시된다.
- null Interactor에는 delegate를 broadcast하지 않는다.
- 신규 Shape의 debug draw가 off일 때 화면에 runtime debug primitive가 나타나지 않는다.

### 11.3 ShipBoardingPoint

- Sphere/Box/Capsule 중 하나를 선택할 수 있다.
- 선택된 Shape만 interaction query에 참여한다.
- 선택된 Shape가 Blueprint Viewport에 보인다.
- Viewport에서 위치·회전·Scale을 조절할 수 있다.
- 기본 Sphere 선택 시 기존 `BP_PlayerShip_Kelvin` 동작과 영역이 유지된다.
- 비선택 Shape 두 개는 `NoCollision`이며 sweep target이 되지 않는다.
- 선박 정책이 boarding을 금지하면 세 Shape 모두 `NoCollision`이다.
- legacy mode가 true이면 기존 radius/transform authoring이 유지된다.
- legacy mode가 false이면 Sphere component template 편집값이 `OnConstruction()`에서 덮어써지지 않는다.

### 11.4 검증 제한

프로젝트 지침에 따라 자동화 테스트와 Headless Editor 실행은 수행하지 않는다. 구현 검증은 전체 컴파일 성공까지 수행하며, Viewport 배치 확인 항목은 에디터 수동 확인 목록으로 남긴다.

### 11.5 수동 에디터 확인 목록

이 목록은 구현 Agent가 실행하지 않고 사용자에게 전달한다.

1. 기존 Sphere 기반 Item/NPC/Storage/Cannon 중 대표 Blueprint가 compile error 없이 열린다.
2. 기존 Sphere radius와 relative transform이 변경 전과 동일하다.
3. ShipBoardingPoint의 `InteractionShape`를 Sphere/Box/Capsule로 바꿀 때 선택 Shape wireframe만 보인다.
4. 각 Shape component를 Components tree에서 선택할 수 있다.
5. Box Extent와 Capsule Radius/Half Height 변경이 Viewport wireframe에 반영된다.
6. PIE에서 선택 Shape만 interaction prompt를 노출한다.
7. 상호작용 입력 시 `AShipBoardingPoint::HandleInteracted()`를 거쳐 `AShip::BoardFromSea()`가 한 번 호출된다.
8. boarding을 허용하지 않는 Ship class에서는 prompt가 나타나지 않는다.
9. 네트워크 환경의 기존 boarding 동작은 회귀가 없는지 별도 수동 확인한다. 이번 변경은 권한 구조를 수정하지 않는다.
10. 9.7의 legacy migration 전후 wireframe을 비교한다.

---

## 12. 제외 사항

본 작업에서는 다음을 구현하지 않는다.

- 상호작용 sweep 알고리즘 변경
- 거리 우선순위 알고리즘 변경
- 서버 거리 재검증 추가
- 신규 권한 또는 네트워크 정책
- 수영 상태 조건 구현
- Actor/Attach Parent 기반 상호작용 대상 resolver
- Custom collision primitive
- 상호작용 영역용 별도 메시 렌더링
- `InteractPopupUIClass`를 `IInteractable`에 추가 노출하는 작업
- 기존 Sphere 기반 Actor를 Box/Capsule로 일괄 변환하는 작업
- Shape 선택의 runtime 변경 및 replication
- legacy BoardingPoint 프로퍼티의 즉시 삭제
- `.uasset` 바이너리 직접 수정
- 신규 automation test 작성 또는 기존 automation test 실행

수영 상태에 따른 `ShipBoardingPoint` 탐지 제외는 Shape 독립화 완료 후 별도 구현 단계로 진행한다.

---

## 13. 실패 및 edge-case 처리

### 13.1 null과 invalid state

- Shape component 포인터가 비정상적으로 null이면 helper는 해당 포인터만 건너뛰고 crash하지 않는다.
- `GetActiveBoardingInteractable()`은 선택된 포인터가 null일 경우 Sphere 포인터 fallback을 반환한다. Sphere도 null이면 null을 반환한다.
- `GetOwningShip()`이 null인 독립 배치 BoardingPoint는 현재 동작과 동일하게 interaction query를 허용한다. 실제 interact 시 `HandleInteracted()`는 아무 작업도 하지 않는다.
- invalid `InteractionShape` enum 값은 Sphere로 취급한다.
- `Interact()`의 null Interactor는 모든 Shape에서 무시한다.

### 13.2 중복 초기화

- delegate 연결은 반드시 `AddUniqueDynamic`을 사용한다.
- construction path에서는 delegate를 bind하지 않는다.
- `InitializeInteractionShapeComponents()`가 실수로 두 번 호출되어도 delegate 중복 broadcast가 생기지 않아야 한다.

### 13.3 collision과 visibility 분리

- visibility는 authoring feedback이며 query eligibility가 아니다.
- query eligibility는 오직 `CollisionEnabled`와 기존 collision profile로 결정한다.
- 비선택 Shape가 보이지 않더라도 `NoCollision` 설정을 생략하지 않는다.
- 선택 Shape가 visible이더라도 선박 정책이 false이면 `NoCollision`이어야 한다.

### 13.4 transform과 scale

- 세 component는 모두 `SceneRoot`에 직접 attach한다. Shape끼리 parent-child로 연결하지 않는다.
- 선택 전환은 transform을 복사하지 않는다. 각 Shape는 독립 authoring 값을 유지한다.
- legacy 값은 Sphere에만 적용한다.
- constructor 또는 state refresh에서 designer-authored scale을 `FVector::OneVector`로 리셋하지 않는다.
- runtime state refresh는 radius, extent, half height, transform을 변경하지 않는다.

### 13.5 UI 다중 Hit

- interactable이 아닌 component Hit는 widget 표시 근거가 될 수 없다.
- 동일 Actor의 일반 collision component만 Hit되고 interactable Shape가 Hit되지 않았다면 prompt를 표시하지 않는다.
- 한 Actor에 여러 interactable Shape가 동시에 활성화되는 잘못된 외부 설정에서도 scan은 crash하지 않고 첫 Hit의 UI 정보를 사용한다.
- 실제 GA target 선택 알고리즘은 계속 거리 기준이며, UI scan의 다중 widget 정책과 통합하지 않는다.

### 13.6 네트워크

- Shape component 자체에 신규 replicated property를 추가하지 않는다.
- `InteractionShape`를 runtime 변경하지 않으므로 replication하지 않는다.
- `AShipBoardingPoint::bReplicates = true`와 `SetReplicateMovement(false)`를 유지한다.
- 서버는 client TargetData의 Hit component에서 기존과 동일하게 `IInteractable`을 cast한다.
- 서버 거리 재검증 부재는 기존 시스템의 별도 보안/정합성 과제이며 이번 작업에서 수정하지 않는다.

---

## 14. 구현 Agent 최종 보고 형식

구현 Agent는 작업 종료 시 다음만 보고한다.

1. 변경한 파일 목록
2. 핵심 구현 결과: 세 Shape 지원, UI concrete-type 제거, BoardingPoint selection/legacy mode
3. compile 명령과 성공/실패 결과
4. 실패 시 상위 3개 error
5. 미실행 항목: automation/PIE/editor asset migration
6. 사용자에게 필요한 9.7 및 11.5 수동 확인

컴파일이 성공해도 Blueprint asset migration을 완료했다고 주장하지 않는다. `.uasset`을 실제로 저장하지 않았다면 C++ 구현 완료와 asset authoring 완료를 명확히 구분한다.

---

## [Implementation Log & Checklist]

- [x] 작업 시작 및 계획 수립
- [x] 1. `Interactable.h`: `FInteractionUIInfo` 전방 선언, `FOnInteractedSignature` 선언 이동, `GetInteractionUIInfo()` 추가
- [x] 2. `InteractableComponent.h` & `.cpp`: delegate 선언 제거, `GetInteractionUIInfo()` 구현, ShapeColor 설정
- [x] 3. `BoxInteractableComponent.h` & `.cpp`: Box 기반 상호작용 컴포넌트 신규 작성
- [x] 4. `CapsuleInteractableComponent.h` & `.cpp`: Capsule 기반 상호작용 컴포넌트 신규 작성
- [x] 5. `BasePlayer.cpp`: `PerformInteractionScan()`의 Hit component 기반 `IInteractable` UI 갱신 구현 및 구체 타입 include 제거
- [x] 6. `ShipBoardingPoint.h` & `.cpp`: Box/Capsule 서브오브젝트, Shape 선택 enum, 레거시 모드 및 상태 갱신 helper 구현
- [x] 7. 정적 검사 수행 (인터페이스 구현 누락, 구체 클래스 재조회 잔존 여부 등)
- [x] 8. `ArtisticSW2026Editor Win64 Development` 컴파일 검증 (성공 - Exit Code 0)
- [x] 9. 최종 보고 및 사용자 수동 에디터 마이그레이션 안내 준비

### 상세 실행 기록
- **2026-09-16**:
  - `Interactable.h`: `struct FInteractionUIInfo;` 전방 선언 및 `FOnInteractedSignature` 델리게이트 선언 이동, `GetInteractionUIInfo()` 순수 가상 함수 추가.
  - `InteractableComponent.h`/`.cpp`: 중복 델리게이트 선언 제거, `GetInteractionUIInfo()` 오버라이드 구현, 생성자에서 `ShapeColor = FColor::Cyan;` 설정.
  - `BoxInteractableComponent.h`/`.cpp`: `UBoxComponent` 및 `IInteractable` 다중 상속 기반 구현, Extent (100, 100, 100), `Interactable` 프로필, Cyan ShapeColor/DebugBox, 유효 Interactor 검사 포함.
  - `CapsuleInteractableComponent.h`/`.cpp`: `UCapsuleComponent` 및 `IInteractable` 다중 상속 기반 구현, Radius 100 / HalfHeight 100, `Interactable` 프로필, Cyan ShapeColor/DebugCapsule, 유효 Interactor 검사 포함.
  - `BasePlayer.cpp`: `PerformInteractionScan()`에서 Hit Component의 `IInteractable` 인터페이스를 통한 UI 조회 알고리즘(8.1) 적용, `UInteractableComponent` 구체 타입 검색 완전 제거 및 `#include "InteractableComponent.h"` 제거.
  - `ShipBoardingPoint.h`/`.cpp`:
    - `EBoardingInteractionShape` (Sphere, Box, Capsule) 열거형 추가
    - `BoardingBoxInteractable`, `BoardingCapsuleInteractable` 기본 서브오브젝트 추가 및 SceneRoot 부착
    - `bUseLegacySphereAuthoring` 플래그 추가 (기본값 true)
    - `GetActiveBoardingInteractable()` generic getter 추가
    - `InitializeInteractionShapeComponents()` helper로 3종 컴포넌트 UI 텍스트 및 OnInteracted 동적 바인딩 일괄 처리
    - `RefreshInteractionShapeState()` helper로 선박 탑승 정책 및 선택된 Shape의 QueryOnly/NoCollision 및 Visibility 동기화
  - **정적 검사**:
    - `public IInteractable`: `UBoxInteractableComponent`, `UInteractableComponent`, `UCapsuleInteractableComponent` 3종 정상 확인
    - `GetInteractionUIInfo`: 선언 및 정의 일치 확인
    - `FindComponentByClass<UInteractableComponent>` in `BasePlayer.cpp`: 0건 확인
  - **컴파일**:
    - 명령: `& "C:\Program Files\Epic Games\UE_5.7\Engine\Build\BatchFiles\Build.bat" ArtisticSW2026Editor Win64 Development -Project="C:\Unreal Projects\ArtisticSW2026\ArtisticSW2026.uproject" -WaitMutex -FromMsBuild -architecture=x64`
    - 결과: `Result: Succeeded`, 종료 코드 0.

