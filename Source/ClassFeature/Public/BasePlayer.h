// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BaseCharacter.h"
#include "GameplayTagContainer.h"
#include "InputTagConfig.h"
#include "BasePlayer.generated.h"

DECLARE_MULTICAST_DELEGATE(FOnAbilitySystemInitializedDelegate);
DECLARE_MULTICAST_DELEGATE(FOnItemSlotsChangedDelegate);

class USpringArmComponent;
class UCameraComponent;
class UInputAction;
struct FInputActionValue;
class ABaseItem;
class UInputTagConfig;
class UInventoryComponent;
class UAnimMontage;

// Item Slot 관리 구조체
USTRUCT(BlueprintType)
struct FItemSlot
{
	GENERATED_BODY()

	// 슬롯에 할당된 GameplayTag (예: key.Item.1)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ItemSlot")
	FGameplayTag KeyTag;

	// 해당 슬롯에 장착된 아이템 객체 포인터
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ItemSlot")
	TObjectPtr<ABaseItem> Item;

	FItemSlot(const FGameplayTag& InTag = FGameplayTag::EmptyTag, ABaseItem* InItem = nullptr);

	// Tag로 배열에서 바로 찾기 위한 연산자 오버로딩
	bool operator==(const FGameplayTag& OtherTag) const;

	// Item 포인터로 배열에서 바로 찾기 위한 연산자 오버로딩
	bool operator==(const ABaseItem* OtherItem) const;
};

/**
 * 
 */
UCLASS(Config = Game)
class CLASSFEATURE_API ABasePlayer : public ABaseCharacter
{
	GENERATED_BODY()
public:
	ABasePlayer();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void PostInitializeComponents() override;

	/* --- GAS 초기화 ---*/
public:
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override { return CachedAbilitySystemComponent.Get(); };

protected:
	UPROPERTY()
	TWeakObjectPtr<class UAbilitySystemComponent> CachedAbilitySystemComponent;

	/* --- 네트워크 초기화 ---*/
public:
	// 네트워크 복제 변수 등록
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// 서버에서 빙의될 때 ASC 초기화
	virtual void PossessedBy(AController* NewController) override;

	// 클라이언트에서 PlayerState가 복제 완료되었을 때 ASC 초기화
	virtual void OnRep_PlayerState() override;

	// ASC 초기화 완료를 알리는 델리게이트 (컴포넌트에게 알리기 위함)
	FOnAbilitySystemInitializedDelegate OnAbilitySystemInitialized;

	/* --- 기본 입력 초기화 ---*/
public:
	// 플레이어 입력 바인딩
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

	// Blueprint에서 호출 가능하도록 노출된 입력 API(필요없으면 지워야 함)
	UFUNCTION(BlueprintCallable, Category = "Input")
	virtual void DoMove(float Right, float Forward);

	UFUNCTION(BlueprintCallable, Category = "Input")
	void StopMoveInput();

	UFUNCTION(BlueprintCallable, Category = "Input")
	virtual void DoLook(float Yaw, float Pitch);

	UFUNCTION(BlueprintCallable, Category = "Input")
	virtual void DoJumpStart();

	UFUNCTION(BlueprintCallable, Category = "Input")
	virtual void DoJumpEnd();

	UFUNCTION(BlueprintCallable, Category = "Movement|Sprint")
	void StartSprint();

	UFUNCTION(BlueprintCallable, Category = "Movement|Sprint")
	void StopSprint();

	/* --- 애니메이션 이동 상태 --- */
public:
	// Animation-only air state. This is the single source of truth for ABP IsAir.
	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement")
	bool bIsInAir = false;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement")
	bool bIsPhysicallyInAir = false;

	// JumpStart is entered by bIsJumping, but JumpStart->FallLoop transition is handled in ABP by Time Remaining Fraction.
	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement")
	bool bIsJumping = false;

	// FallOffStart means entering air without jump input, such as walking off a ledge.
	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement")
	bool bIsFallOffStart = false;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement")
	bool bIsLanding = false;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement")
	bool bLandingRequested = false;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement")
	bool bCanEnterLand = false;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement")
	bool bCanEnterGround = true;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement")
	float GroundSpeed = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement")
	float VerticalSpeed = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement|Input")
	bool bHasMoveInput = false;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement|Input")
	bool bPrevHasMoveInput = false;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement|Input")
	float MoveInputSize = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Movement|Input")
	float MoveInputDeadZone = 0.1f;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement|Input")
	FVector2D CachedMoveInput = FVector2D::ZeroVector;

	UPROPERTY(BlueprintReadWrite, Category = "Animation|Movement|Sprint")
	bool bIsSprinting = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Movement|Sprint")
	float WalkSpeed = 500.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Movement|Sprint")
	float SprintSpeed = 700.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Movement|Sprint")
	float WalkRotationRateYaw = 500.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Movement|Sprint")
	float SprintRotationRateYaw = 500.f;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement|Start")
	float MoveInputHeldTime = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Movement|Start")
	float StartToLoopDelay = 0.75f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Movement|Start")
	float MinStartDatabaseTime = 0.12f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Movement|Start")
	float SprintStartToLoopDelay = 0.34f;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement|Start")
	float CurrentStartToLoopDelay = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement|Start")
	bool bUseStartDatabase = false;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement|Start")
	bool bGroundStartFinished = false;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement|Start")
	bool bPendingGroundStartFinish = false;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement|Start")
	bool bStartWasSprinting = false;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement|Start")
	bool bUseLoopDatabase = false;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement|Turn")
	bool bUseSharpTurnDatabase = false;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement|Requests")
	bool bStartRequested = false;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement|Requests")
	bool bStopRequested = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Movement|Requests")
	float StopIntentSpeedThreshold = 80.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Movement|Requests")
	float IdleSpeedThreshold = 30.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Movement|Requests")
	float RunToSprintSpeedThreshold = 500.f;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement|Turn")
	float MoveInputTurnAngle = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement|Turn")
	bool bSharpTurnRequested = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Movement|Turn")
	float SharpTurnAngleThreshold = 60.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Movement|Turn")
	float MoveInputTurnDeadZoneAngle = 5.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Movement|Turn")
	float SharpTurnMinSpeed = 500.f;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement|Turn")
	FVector2D PreviousMoveInputForTurn = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Movement")
	float JumpStartDuration = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Movement")
	float FallOffStartDuration = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Movement")
	float LandingDuration = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Movement|Landing")
	float LandingRequestDuration = 0.45f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Movement|Landing")
	float JumpStartMaxDuration = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement|Landing")
	float LastFallSpeed = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement|Landing")
	float LandStartGroundSpeed = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement|Landing")
	float LandStartFallSpeed = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement|Landing")
	bool bLandWasMoving = false;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement|Landing")
	bool bLandWasSprinting = false;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement|Landing")
	bool bUseHeavyLand = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Movement|Landing")
	float HeavyLandSpeedThreshold = 650.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Movement|Landing")
	float RealLandingEventSpeedThreshold = 300.f;

	/* --- 애니메이션 전투 상태 --- */
	UPROPERTY(BlueprintReadOnly, Category = "Animation|Combat")
	bool bIsCombatMode = false;

	UPROPERTY(BlueprintReadWrite, Category = "Animation|Combat")
	bool bIsAttacking = false;

	UPROPERTY(BlueprintReadWrite, Category = "Animation|Combat")
	bool bIsDodging = false;

	UPROPERTY(BlueprintReadWrite, Category = "Animation|Combat")
	bool bIsHitReacting = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Combat")
	TObjectPtr<UAnimMontage> CombatIntroMontage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Combat")
	float CombatIntroMontagePlayRate = 1.f;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Combat")
	bool bIsPlayingCombatIntro = false;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Combat")
	bool bPendingCombatModeFromIntro = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Combat")
	bool bInterruptCombatIntroOnHit = true;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Combat")
	float MovementDirection = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Combat")
	float CombatInputForward = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Combat")
	float CombatInputRight = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Combat")
	float CombatForwardSpeed = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Combat")
	float CombatRightSpeed = 0.f;

	bool IsInAirForAnimation() const;

	UFUNCTION(BlueprintCallable, Category = "Animation|Movement")
	void FinishFallOffStart();

	UFUNCTION(BlueprintCallable, Category = "Animation|Movement")
	void FinishJumpStart();

	UFUNCTION(BlueprintCallable, Category = "Animation|Movement|Start")
	void MarkGroundStartFinished();

	UFUNCTION(BlueprintCallable, Category = "Animation|Combat")
	void RequestCombatModeToggle();

	UFUNCTION(BlueprintCallable, Category = "Animation|Combat")
	void SetCombatMode(bool bNewCombatMode);

	UFUNCTION(BlueprintCallable, Category = "Animation|Combat")
	void InterruptCombatIntroForHit();

protected:
	bool bWasInAir = false;

	// Prevents a normal jump from being misclassified as FallOffStart.
	bool bSuppressFallOffStart = false;

	FTimerHandle JumpStartTimerHandle;
	FTimerHandle FallOffStartTimerHandle;
	FTimerHandle LandingTimerHandle;
	FTimerHandle LandingRequestTimerHandle;

	void UpdateAnimationMovementState(float DeltaTime);
	void UpdateMovementRequestState(float DeltaTime);
	void UpdateCombatMovementState();
	void UpdateMaxWalkSpeed();
	void ClearMovementRequests();
	void ApplyCombatRotationMode(bool bEnableCombatRotation);
	void StartFallOffStart();
	void StopFallOffStart();
	void FinishLanding();
	void FinishLandingRequest();
	void OnCombatIntroMontageEnded(UAnimMontage* Montage, bool bInterrupted);

	// 태그를 넣으면 고유 Hash 기반 ID를 반환하는 헬퍼
	int32 GetInputIDFromTag(const FGameplayTag& Tag) const;

protected:
	// 서버에 의해 로컬에서 Controller가 조종하는 Pawn이 지정될 때 호출되는 함수.
	virtual void PawnClientRestart() override;


	// Default IMC (마우스 등)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<class UInputMappingContext> DefaultIMC;

	// Default IMC의 우선순위
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	int32 DefaultIMCPriority = 1;

	// Default IA - Tag 매핑 DA
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputTagConfig> DefaultInputConfig;

	// Input Action
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> JumpAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> MoveAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> LookAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> MouseLookAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> SprintAction;

	void Move(const FInputActionValue& Value);
	void MoveStopped(const FInputActionValue& Value);
	void Look(const FInputActionValue& Value);

	// 기본 착지 이벤트 오버라이드
	virtual void Landed(const FHitResult& Hit) override;

	// C++에서 '진짜 착지'로 판정되었을 때 블루프린트(ABP)로 신호를 보내기 위한 이벤트
	UFUNCTION(BlueprintImplementableEvent, Category = "Movement|Animation")
	void K2_OnRealLanded();

	/* --- 키 입력으로 실행되는 GA ---  */
public:
	// Default GA가 어느 Key(Tag)에 매핑될지 설정하는 Map
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Abilities")
	TMap<FGameplayTag, TSubclassOf<UGameplayAbility>> DefaultAbilityMap;

	// GA와 그 GA가 어떤 키 입력(Tag)에 반응할지 함께 적용하는 함수.
	UFUNCTION(BlueprintCallable, Category = "Abilities")
	void GrantAbilityToSlot(FGameplayTag SlotTag, TSubclassOf<UGameplayAbility> AbilityClass);

	// 키보드 & 마우스 입력 Tag와 함께 등록된 GA 해제
	UFUNCTION(BlueprintCallable, Category = "Abilities")
	void RemoveAbilityFromSlot(FGameplayTag SlotTag);

	// 즉발형 GA에 대해 SlotTag에 매핑된 GA를 실행하는 함수
	void OnAbilityInputPressed(FGameplayTag InputTag);
	void OnAbilityInputReleased(FGameplayTag InputTag);

	// 마우스 입력에 대한 활용을 위해 따로 OnAbilityInput과 분리
	void OnMouseInputPressed(FGameplayTag InputTag);
	void OnMouseInputReleased(FGameplayTag InputTag);

	// 서버의 GA에게 GameplayEvent를 보내는 함수 (예: 마우스 입력에 반응하는 GA에게 신호 보내기)
	UFUNCTION(Server, Reliable)
	void ServerRPC_SendGameplayEvent(FGameplayTag EventTag, FGameplayEventData Payload);

	/* --- ItemSlot ---  */ 
public:
	// 현재 캐릭터가 장착하고 있는 아이템
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_EquippedItem, Category = "Equipment")

	TObjectPtr<ABaseItem> EquippedItem;

	// EquippedItem의 변경이 복제되었을 때 호출되는 함수
	UFUNCTION()
	void OnRep_EquippedItem();

	// ItemSlot 구조체 배열 (복제)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_ItemSlots, Category = "Item")
	TArray<FItemSlot> ItemSlots;

	// 특정 슬롯의 Item을 제거하고 부여된 GA를 회수
	UFUNCTION()
	void RemoveItemFromSlot(FGameplayTag SlotTag);

	UFUNCTION()
	void UseEquippedItem(bool bDestroy = true);

	// 빈 아이템 슬롯이 하나라도 있는지 확인
	UFUNCTION()
	bool HasEmptyItemSlot() const;

	// 아이템 슬롯에 아이템을 저장하고 장착 상태를 관리
	bool TryPutItemInSlot(ABaseItem* Item);

protected:
	// IA와 Slot Tag의 Mapping 정보가 담긴 DataAsset (BP 주입)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputTagConfig> ItemInputConfig;

	// Item IMC
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<class UInputMappingContext> ItemIMC;

	// Item IMC의 우선순위
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	int32 ItemIMCPriority = 1;

	// 슬롯 키를 눌렀을 때 아이템을 장착하는 함수
	void EquipItemFromSlot(FGameplayTag SlotTag);

	// 서버에서 먼저 ItemSlot 처리를 해준 후 클라이언트가 수행하기 위해
	UFUNCTION(Server, Reliable)
	void Server_EquipItemFromSlot(FGameplayTag KeyTag);

	// 공용 Interact GA가 보내준 PickUp 이벤트를 처리하는 함수
	void HandlePickUpEvent(const FGameplayEventData* Payload);


	/* --- Interactable Object Trace ---*/
public:
	// Interactable Object 감지를 위한 범위 함수 (범위 내의 모든 HitResult를 반환하므로 알아서 걸러 쓸 것)
	bool PerformInteractTrace(TArray<FHitResult>& OutHitResults) const;

protected:
	// DefaultGame.ini에서 제어할 스캔 주기 (단위: 초)
	UPROPERTY(Config, EditAnywhere, Category = "Interaction")
	float InteractionScanInterval = 0.1f;

	UPROPERTY(EditDefaultsOnly, Category = "Interaction")
	float InteractTraceDistance = 100.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Interaction")
	float InteractTraceRadius = 30.0f;

	// 스캔 타이머 핸들
	FTimerHandle InteractionScanTimerHandle;

	// 현재 화면에 띄운 Interactable Obj의 WidgetComp들을 캐시 (WeakPtr)
	TArray<TWeakObjectPtr<class UWidgetComponent>> CachedHoveredWidgets;

	// 스캔 시작 함수
	void StartInteractionScan();

	// 타이머에 의해 반복 호출될 실제 UI 갱신 로직
	void PerformInteractionScan();


	/* --- 카메라 ---*/
public:
	FORCEINLINE USpringArmComponent* GetCameraBoom() const { return CameraBoom; }
	FORCEINLINE UCameraComponent* GetFollowCamera() const { return FollowCamera; }
protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCameraComponent> FollowCamera;

	// 평상시 카메라 거리
	UPROPERTY(EditDefaultsOnly, Category = "Camera")
	float DefaultTargetArmLength = 400.f;

	// 조준 시 카메라 거리
	UPROPERTY(EditDefaultsOnly, Category = "Camera")
	float AimingTargetArmLength = 150.f;

	// 평상시 카메라 오프셋
	UPROPERTY(EditDefaultsOnly, Category = "Camera")
	FVector DefaultSocketOffset = FVector(0.f, 0.f, 0.f);

	// 조준 시 카메라 오프셋
	UPROPERTY(EditDefaultsOnly, Category = "Camera")
	FVector AimingSocketOffset = FVector(0.f, 60.f, 50.f);

	// 스나이핑 조준 시 카메라 거리 (1인칭: 0)
	UPROPERTY(EditDefaultsOnly, Category = "Camera")
	float SnipingTargetArmLength = 0.f;

	// 스나이핑 조준 시 카메라 오프셋 (캐릭터 눈높이)
	UPROPERTY(EditDefaultsOnly, Category = "Camera")
	FVector SnipingSocketOffset = FVector(0.f, 0.f, 70.f);

	// 스나이핑 마우스 감도
	UPROPERTY(EditDefaultsOnly, Category = "Camera")
	float SnipingMouseSensitivity = 0.5f;

	// 기본 FOV
	UPROPERTY(EditDefaultsOnly, Category = "Camera")
	float DefaultFOV = 90.f;

	// 스나이핑 시 FOV (줄일수록 더 확대)
	UPROPERTY(EditDefaultsOnly, Category = "Camera")
	float SnipingFOV = 30.f;

	// 카메라 전환 보간 속도
	UPROPERTY(EditDefaultsOnly, Category = "Camera")
	float CameraInterpSpeed = 10.f;

	/* --- 인벤토리 ---*/
protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Inventory")
	TObjectPtr<UInventoryComponent> InventoryComponent;

public:
	UFUNCTION()
	void OnRep_ItemSlots();

	FOnItemSlotsChangedDelegate OnItemSlotsChanged;

	UFUNCTION(BlueprintPure, Category = "Inventory")
	UInventoryComponent* GetInventoryComponent() const { return InventoryComponent; }
};
