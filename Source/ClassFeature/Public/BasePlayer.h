// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BaseCharacter.h"
#include "GameplayTagContainer.h"
#include "InputTagConfig.h"
#include "Equipment/PlayerEquipmentComponent.h"
#include "Animation/LocomotionAnimStateComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "Skills/SkillUseProvider.h"
#include "BasePlayer.generated.h"

DECLARE_MULTICAST_DELEGATE(FOnAbilitySystemInitializedDelegate);
DECLARE_MULTICAST_DELEGATE(FOnItemSlotsChangedDelegate);
DECLARE_MULTICAST_DELEGATE(FOnQuickSlotsChangedDelegate);

class USpringArmComponent;
class UCameraComponent;
class UInputAction;
struct FInputActionValue;
class ABaseItem;
class UInputTagConfig;
class UInventoryComponent;
class UCraftingComponent;
class USWTrajectoryComponent;
class UAnimMontage;
class UBaseHealthComponent;
class AShip;
class ACannon;
class USwimmingComponent;
class UPlayerSkillComponent;

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

UENUM(BlueprintType)
enum class EQuickSlotType : uint8
{
	Weapon,
	Consumable
};

/** Inventory-backed quick-slot reference. The item remains in the inventory. */
USTRUCT(BlueprintType)
struct FQuickSlotReference
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "QuickSlot")
	FGameplayTag KeyTag;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "QuickSlot")
	FGameplayTag ItemTag;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "QuickSlot")
	EQuickSlotType SlotType = EQuickSlotType::Weapon;

	bool IsEmpty() const { return !ItemTag.IsValid(); }
};

/**
 * 
 */
UCLASS(Config = Game)
class CLASSFEATURE_API ABasePlayer : public ABaseCharacter, public ISkillUseProvider
{
	GENERATED_BODY()
	friend class ULocomotionAnimStateComponent;

public:
	ABasePlayer(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void PostInitializeComponents() override;

	UFUNCTION()
	void HandleDeathFinished(UBaseHealthComponent* InHealthComponent);

	/* --- GAS 초기화 ---*/
public:
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override { return CachedAbilitySystemComponent.Get(); };

	// ISkillUseProvider: execution actors call this bridge without depending on ClassFeature.
	virtual bool CanUseSkill(const FGameplayTag& SkillTag) const override;
	virtual bool TryConsumeSkillUse(const FGameplayTag& SkillTag) override;

	UFUNCTION(BlueprintPure, Category = "Skill")
	UPlayerSkillComponent* GetPlayerSkillComponent() const;

protected:
	UPROPERTY()
	TWeakObjectPtr<class UAbilitySystemComponent> CachedAbilitySystemComponent;

	/** Retained while the controller temporarily possesses a ship or cannon. */
	UPROPERTY()
	TWeakObjectPtr<UPlayerSkillComponent> CachedPlayerSkillComponent;

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

	/** Hold Ctrl while swimming to descend. This is intentionally inert on land. */
	UFUNCTION(BlueprintCallable, Category = "Input|Swimming")
	void StartSwimDive();

	UFUNCTION(BlueprintCallable, Category = "Input|Swimming")
	void StopSwimDive();

	UFUNCTION(BlueprintCallable, Category = "Movement|Sprint")
	void StartSprint();

	UFUNCTION(BlueprintCallable, Category = "Movement|Sprint")
	void StopSprint();

	UFUNCTION(Server, Reliable)
	void Server_SetSprinting(bool bNewSprinting);

	UFUNCTION(Server, Reliable)
	void Server_SetMoveInput(FVector2D NewMoveInput);

	UFUNCTION(Server, Reliable)
	void Server_SetSwimmingVerticalInput(float NewVerticalInput);


	UFUNCTION()
	void OnRep_LocomotionStateSnapshot(const FReplicatedLocomotionState& OldSnapshot);

	UFUNCTION(Server, Reliable)
	void Server_NotifyJumpStarted();

	// Multicast RPCs 제거됨 (데이터 기반 이벤트 처리로 변경)

	void BroadcastFallOffStartedForRemoteClients();

	/* --- 애니메이션 이동 상태 --- */
public:
	FVector2D LastSentMoveInputToServer = FVector2D::ZeroVector;
	bool bHasSentMoveInputToServer = false;
	FVector2D AuthoritativeMoveInput = FVector2D::ZeroVector;
	bool bHasAuthoritativeMoveInput = false;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Movement|Sprint")
	bool bSprintInputHeld = false;

	bool bSwimDiveInputHeld = false;
	bool bSwimAscendInputHeld = false;

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_LocomotionStateSnapshot, Category = "Animation|Movement|Network")
	FReplicatedLocomotionState LocomotionStateSnapshot;

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

	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> ActiveCombatIntroMontage;

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

	UFUNCTION(BlueprintCallable, Category = "Animation|Combat")
	void RequestCombatModeToggle();

	UFUNCTION(BlueprintCallable, Category = "Animation|Combat")
	void SetCombatMode(bool bNewCombatMode);

	UFUNCTION(BlueprintCallable, Category = "Animation|Combat")
	void EnterCombatModeFromEquipment();

	UFUNCTION(BlueprintCallable, Category = "Animation|Combat")
	void InterruptCombatIntroForHit();

	/** Keep server-side weapon sockets synchronized while a combat montage is active. */
	void AcquireServerCombatPoseRefresh();

	/** Releases one combat pose refresh request and restores the previous mesh setting. */
	void ReleaseServerCombatPoseRefresh();

protected:
	EVisibilityBasedAnimTickOption ServerCombatOriginalAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPose;
	int32 ServerCombatPoseRefreshRefCount = 0;

	int32 LocomotionAnimEventSequence = 0;
	void UpdateLocomotionStateSnapshot();
	int32 NextLocomotionAnimEventSequence();
	bool CanSprintFromInput() const;
	void RefreshSprintFromInput();
	bool CanSprintFromServerState() const;
	void ApplyCombatRotationMode(bool bEnableCombatRotation);
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
	void RefreshSwimmingVerticalInput();

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

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Abilities")
	TArray<TSubclassOf<UGameplayAbility>> DefaultGrantedAbilities;

	/**
	 * Development-only convenience switch for skill testing.
	 * When enabled, all three player skills ignore story locks and inventory
	 * materials, and completed uses do not consume an item.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Abilities|Skill Test")
	bool bBypassSkillRequirementsForTesting = false;

	/** Temporary Keyboard 3 test hook; disable when the final skill slot is wired. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Abilities|Gravity Vortex Test")
	bool bEnableGravityVortexTestInput = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Abilities|Gravity Vortex Test")
	TSubclassOf<UGameplayAbility> GravityVortexTestAbilityClass;

	/** Granted without a player input slot; a ridden cannon activates/cancels it through its ability tag. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Abilities|Water Bomb")
	bool bGrantWaterBombAbility = true;

	/** Set this to a GA_WaterBombCannonMode Blueprint to tune projectile, duration, and slow multiplier. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Abilities|Water Bomb")
	TSubclassOf<UGameplayAbility> WaterBombAbilityClass;

	/** Granted without a player input slot; the currently possessed ship toggles it with test key 5. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Abilities|Bombardment")
	bool bGrantBombardmentAbility = true;

	/** Set this to a GA_Bombardment Blueprint that references the authored Bombardment actor class. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Abilities|Bombardment")
	TSubclassOf<UGameplayAbility> BombardmentAbilityClass;

	// GA와 그 GA가 어떤 키 입력(Tag)에 반응할지 함께 적용하는 함수.
	UFUNCTION(BlueprintCallable, Category = "Abilities")
	void GrantAbilityToSlot(FGameplayTag SlotTag, TSubclassOf<UGameplayAbility> AbilityClass);

	UFUNCTION(BlueprintCallable, Category = "Abilities")
	void GrantDefaultAbility(TSubclassOf<UGameplayAbility> AbilityClass);

	// 키보드 & 마우스 입력 Tag와 함께 등록된 GA 해제
	UFUNCTION(BlueprintCallable, Category = "Abilities")
	void RemoveAbilityFromSlot(FGameplayTag SlotTag);

	// 즉발형 GA에 대해 SlotTag에 매핑된 GA를 실행하는 함수
	void OnAbilityInputPressed(FGameplayTag InputTag);
	void OnAbilityInputReleased(FGameplayTag InputTag);
	void OnGravityVortexTestPressed();
	void OnGravityVortexTestReleased();

	// 마우스 입력에 대한 활용을 위해 따로 OnAbilityInput과 분리
	void OnMouseInputPressed(FGameplayTag InputTag);
	void OnMouseInputReleased(FGameplayTag InputTag);
	void AddMouseAimTargetData(FGameplayEventData& EventData) const;

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


	UFUNCTION(BlueprintPure, Category = "Equipment")
	EEquipmentState GetEquipmentState() const;

	UFUNCTION(BlueprintPure, Category = "Equipment")
	bool IsEquipmentTransitioning() const;

	UFUNCTION(BlueprintCallable, Category = "Equipment")
	void HandleEquipmentAttachNotify();

	// ItemSlot 援ъ“泥?諛곗뿴 (蹂듭젣)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_ItemSlots, Category = "Item")
	TArray<FItemSlot> ItemSlots;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_QuickSlots, Category = "QuickSlot")
	TArray<FQuickSlotReference> QuickSlots;

	FOnQuickSlotsChangedDelegate OnQuickSlotsChanged;

	UFUNCTION(BlueprintCallable, Category = "QuickSlot")
	void AssignQuickSlotFromInventory(int32 QuickSlotIndex);

	UFUNCTION(BlueprintCallable, Category = "QuickSlot")
	void ClearQuickSlot(int32 QuickSlotIndex);

	UFUNCTION(BlueprintCallable, Category = "QuickSlot")
	void ActivateQuickSlot(int32 QuickSlotIndex);

	UFUNCTION(BlueprintPure, Category = "QuickSlot")
	bool CanQuickSlotAcceptItem(int32 QuickSlotIndex, FGameplayTag ItemTag) const;

	UFUNCTION(Server, Reliable)
	void ServerAssignQuickSlotFromInventory(int32 QuickSlotIndex);

	UFUNCTION(Server, Reliable)
	void ServerClearQuickSlot(int32 QuickSlotIndex);

	UFUNCTION(Server, Reliable)
	void ServerActivateQuickSlot(int32 QuickSlotIndex);

	// 특정 슬롯의 Item을 제거하고 부여된 GA를 회수
	// ?뱀젙 ?щ’??Item???쒓굅?섍퀬 遺?щ맂 GA瑜??뚯닔
	UFUNCTION()
	void RemoveItemFromSlot(FGameplayTag SlotTag);

	UFUNCTION()
	void UseEquippedItem(bool bDestroy = true);

	// 鍮??꾩씠???щ’???섎굹?쇰룄 ?덈뒗吏 ?뺤씤
	UFUNCTION()
	bool HasEmptyItemSlot() const;

	// ?꾩씠???щ’???꾩씠?쒖쓣 ??ν븯怨??μ갑 ?곹깭瑜?愿由?
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
	void ActivateQuickSlot1();
	void ActivateQuickSlot2();
	void ActivateQuickSlot3();
	void ActivateQuickSlot4();
	void ActivateQuickSlot5();
	void InitializeQuickSlots();
	void UnequipCurrentItem();
	bool EquipInventoryWeapon(FGameplayTag ItemTag);
	bool ConsumeInventoryItem(FGameplayTag ItemTag);
	void HandleInventoryContentsChanged();
	bool IsEquippedItemOwnedByLegacySlot() const;

	// 서버에서 먼저 ItemSlot 처리를 해준 후 클라이언트가 수행하기 위해
	UFUNCTION(Server, Reliable)
	void Server_EquipItemFromSlot(FGameplayTag KeyTag);

	// 공용 Interact GA가 보내준 PickUp 이벤트를 처리하는 함수
	void HandlePickUpEvent(const FGameplayEventData* Payload);

	// 배 승선 이벤트를 처리하는 함수
	void HandleShipBoardEvent(const FGameplayEventData* Payload);

	// 대포 탑승 이벤트를 처리하는 함수
	void HandleCannonBoardEvent(const FGameplayEventData* Payload);

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

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Crafting")
	TObjectPtr<UCraftingComponent> CraftingComponent;
	/** 에디터 테스트 시작 시 특정 아이템을 인벤토리에 지급한다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Testing|Inventory")
	bool bGiveStartingItemForTest = false;

	/** DA_ItemData에 등록된 구체적인 Item.Id 태그를 지정한다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Testing|Inventory",
		meta = (EditCondition = "bGiveStartingItemForTest", Categories = "Item.Id"))
	FGameplayTag StartingItemTagForTest;

	/** 테스트 시작 시 보유하게 할 총수량이다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Testing|Inventory",
		meta = (EditCondition = "bGiveStartingItemForTest", ClampMin = "1", UIMin = "1"))
	int32 StartingItemCountForTest = 1;

	void GiveStartingItemForTest();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation")
	TObjectPtr<ULocomotionAnimStateComponent> AnimStateComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation")
	TObjectPtr<USWTrajectoryComponent> TrajectoryComponent;

	/**
	 * Optional linked animation-layer implementation used while the character is
	 * swimming. Assign ABP_Swim (or a character-specific equivalent) in the
	 * player Blueprint; the native BeginPlay path binds its implemented layers.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation|Swimming")
	TSubclassOf<class UAnimInstance> SwimmingAnimLayerClass;

	/** Binds SwimmingAnimLayerClass to this mesh's main animation instance. */
	void InitializeSwimmingAnimLayers();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UBaseHealthComponent> HealthComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USwimmingComponent> SwimmingComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UPlayerEquipmentComponent> EquipmentComponent;

public:
	UFUNCTION()
	void OnRep_ItemSlots();

	UFUNCTION()
	void OnRep_QuickSlots();

	FOnItemSlotsChangedDelegate OnItemSlotsChanged;

	UFUNCTION(BlueprintPure, Category = "Inventory")
	UInventoryComponent* GetInventoryComponent() const { return InventoryComponent; }

	UFUNCTION(BlueprintPure, Category = "Crafting")
	UCraftingComponent* GetCraftingComponent() const { return CraftingComponent; }

	UFUNCTION(BlueprintPure, Category = "Animation")
	ULocomotionAnimStateComponent* GetAnimStateComponent() const { return AnimStateComponent; }

	UFUNCTION(BlueprintPure, Category = "Health")
	UBaseHealthComponent* GetHealthComponent() const { return HealthComponent; }

	UFUNCTION(BlueprintPure, Category = "Movement")
	USwimmingComponent* GetSwimmingComponent() const { return SwimmingComponent; }

	UFUNCTION(BlueprintPure, Category = "Equipment")
	UPlayerEquipmentComponent* GetEquipmentComponent() const { return EquipmentComponent; }
};
