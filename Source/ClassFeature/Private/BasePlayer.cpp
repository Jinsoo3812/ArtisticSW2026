// Fill out your copyright notice in the Description page of Project Settings.


#include "BasePlayer.h"
#include "BasePlayerState.h"
#include "Misc/Crc.h"
#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbilityTargetTypes.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"
#include "InputCoreTypes.h"
#include "BaseItem.h"
#include "CrafterComponent.h"
#include "BaseGameplayTags.h"
#include "Net/UnrealNetwork.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "SWCharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "ItemData.h"
#include "Interactable.h"
#include "CollisionChannels.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "Components/WidgetComponent.h"
#include "InteractableComponent.h"
#include "InteractUserWidget.h"
#include "Animation/LocomotionAnimStateComponent.h"
#include "Animation/SWTrajectoryComponent.h"
#include "Inventory/InventoryComponent.h"
#include "Crafting/CraftingComponent.h"
#include "ItemSubSystem.h"
#include "Equipment/PlayerEquipmentComponent.h"
#include "Item/Components/BowComponent.h"
#include "Item/Weapons/BowItem.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/BaseHealthComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Ship.h"
#include "Cannon.h"
#include "SwimmingComponent.h"
#include "Attacker/GA_GravityVortexThrow.h"
#include "Attacker/GA_WaterBombCannonMode.h"
#include "HAL/FileManager.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	bool IsBasePlayerMotionMatchingCaptureEnabled()
	{
		const IConsoleVariable* DebugCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("p.MMDebugging"));
		return DebugCVar && DebugCVar->GetInt() > 0;
	}

	void AppendBasePlayerMotionMatchingCaptureLine(const FString& Line)
	{
		// 파일 입출력을 제거하여 퍼포먼스 드랍 방지. 기존 UE_LOG로 대체됨.
	}
}

/* --- FItemSlot ---*/

FItemSlot::FItemSlot(const FGameplayTag& InTag, ABaseItem* InItem)
	: KeyTag(InTag), Item(InItem) {}

bool FItemSlot::operator==(const FGameplayTag& OtherTag) const { return KeyTag == OtherTag; }

bool FItemSlot::operator==(const ABaseItem* OtherItem) const { return Item.Get() == OtherItem; }

// 커스텀 어태치 규칙 생성: 위치(Snap), 회전(Snap), 스케일(KeepWorld)
FAttachmentTransformRules CustomAttachRules(
	EAttachmentRule::SnapToTarget,   // Location: 소켓 위치에 맞춤
	EAttachmentRule::SnapToTarget,   // Rotation: 소켓 회전에 맞춤
	EAttachmentRule::KeepWorld,      // Scale: 부모 스케일 무시하고 현재 월드 크기(0.15) 절대 유지
	false                            // bWeldSimulatedBodies: 콜리전 병합 여부
);

/* --- BasePlayer ---*/

ABasePlayer::ABasePlayer(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<USWCharacterMovementComponent>(ACharacter::CharacterMovementComponentName))
{
	// 카메라 붐(SpringArm) 생성 및 설정
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 400.0f;
	CameraBoom->bUsePawnControlRotation = true;

	// Follow 카메라 생성 및 설정
	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;

	// 아이템 포인터 초기화
	EquippedItem = nullptr;
	CameraBoom->bDoCollisionTest = false;

	// 인벤토리 컴포넌트 부착
	InventoryComponent = CreateDefaultSubobject<UInventoryComponent>(TEXT("InventoryComponent"));
	CraftingComponent = CreateDefaultSubobject<UCraftingComponent>(TEXT("CraftingComponent"));
	AnimStateComponent = CreateDefaultSubobject<ULocomotionAnimStateComponent>(TEXT("AnimStateComponent"));
	TrajectoryComponent = CreateDefaultSubobject<USWTrajectoryComponent>(TEXT("TrajectoryComponent"));
	HealthComponent = CreateDefaultSubobject<UBaseHealthComponent>(TEXT("HealthComponent"));
	SwimmingComponent = CreateDefaultSubobject<USwimmingComponent>(TEXT("SwimmingComponent"));
	EquipmentComponent = CreateDefaultSubobject<UPlayerEquipmentComponent>(TEXT("EquipmentComponent"));
	GravityVortexTestAbilityClass = UGA_GravityVortexThrow::StaticClass();
	WaterBombAbilityClass = UGA_WaterBombCannonMode::StaticClass();

	// 항상 등만 보이도록 설정 (Orient to Controller - 부드러운 회전으로 제자리 회전 유도)
	bUseControllerRotationYaw = false;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;

	if (UCharacterMovementComponent* MovementComponent = GetCharacterMovement())
	{
		MovementComponent->MaxWalkSpeed = AnimStateComponent ? AnimStateComponent->WalkSpeed : 500.f;
		MovementComponent->RotationRate = FRotator(0.f, AnimStateComponent ? AnimStateComponent->WalkRotationRateYaw : 500.f, 0.f);

		// 캐릭터가 움직이면서 물리 오브젝트에 힘을 가하지 못하도록 설정.
		MovementComponent->bEnablePhysicsInteraction = false;

		// 캐릭터 충돌 캡슐이 다른 강체와 접촉했을 때 힘을 가하지 않도록 차단.
		MovementComponent->bTouchForceScaledToMass = false;

		// 힘의 계수를 0으로 설정
		MovementComponent->InitialPushForceFactor = 0.0f;
		MovementComponent->PushForceFactor = 0.0f;

		// 이동 방향으로 몸 회전 방지
		MovementComponent->bOrientRotationToMovement = false;
		// 컨트롤러 지향 방향으로 부드러운 정렬 사용
		MovementComponent->bUseControllerDesiredRotation = true;
	}
}

void ABasePlayer::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// 배열과 장착 아이템 포인터를 클라이언트로 복제
	DOREPLIFETIME(ABasePlayer, ItemSlots);
	DOREPLIFETIME(ABasePlayer, QuickSlots);
	DOREPLIFETIME(ABasePlayer, EquippedItem);
	DOREPLIFETIME(ABasePlayer, LocomotionStateSnapshot);
}

void ABasePlayer::PostInitializeComponents()
{
	Super::PostInitializeComponents();
}

void ABasePlayer::BeginPlay()
{
	Super::BeginPlay();

	if (HealthComponent)
	{
		HealthComponent->OnDeathFinished.AddUniqueDynamic(this, &ABasePlayer::HandleDeathFinished);
		if (HealthComponent->GetDeathState() == EBaseDeathState::DeathFinished)
		{
			ApplyLocalDeathRagdoll();
		}
	}

	// ItemSlot 배열 초기화: TMap 등록 없이 구조체 배열에 순서대로 Add
	if (ItemInputConfig)
	{
		ItemSlots.Empty();

		for (const FKeyInputAction& Action : ItemInputConfig->KeyInputActions)
		{
			if (Action.KeyTag.IsValid() && Action.KeyTag.MatchesTag(Key_Item))
			{
				ItemSlots.Add(FItemSlot(Action.KeyTag));
			}
		}
	}

	InitializeQuickSlots();
	if (InventoryComponent)
	{
		InventoryComponent->OnInventoryChanged.AddUObject(this, &ABasePlayer::HandleInventoryContentsChanged);
	}

	OnItemSlotsChanged.Broadcast();
	OnQuickSlotsChanged.Broadcast();
}

void ABasePlayer::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (InventoryComponent)
	{
		InventoryComponent->OnInventoryChanged.RemoveAll(this);
	}

	if (HealthComponent)
	{
		HealthComponent->OnDeathFinished.RemoveDynamic(this, &ABasePlayer::HandleDeathFinished);
		HealthComponent->UninitializeFromAbilitySystem();
	}

	Super::EndPlay(EndPlayReason);
}

void ABasePlayer::HandleDeathFinished(UBaseHealthComponent* InHealthComponent)
{
	ApplyLocalDeathRagdoll();
}

void ABasePlayer::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// 후방 이동 시 질주(Sprint) 차단 (1안)
	RefreshSprintFromInput();

	if (AnimStateComponent)
	{
		AnimStateComponent->UpdateAnimationState(DeltaTime);
	}
	if (HasAuthority())
	{
		UpdateLocomotionStateSnapshot();
	}

	bool bIsSniping = false;
	bool bIsAiming = false;
	bool bIsThrowingOrAttacking = false;

	if (CachedAbilitySystemComponent.IsValid())
	{
		bIsSniping = CachedAbilitySystemComponent->HasMatchingGameplayTag(State_Sniping);
		bIsAiming = CachedAbilitySystemComponent->HasMatchingGameplayTag(State_Aiming);
		bIsThrowingOrAttacking = CachedAbilitySystemComponent->HasMatchingGameplayTag(State_Attacking);
	}

	float TargetArmLength = DefaultTargetArmLength;
	FVector TargetSocketOffset = DefaultSocketOffset;

	if (bIsSniping)
	{
		TargetArmLength = SnipingTargetArmLength;
		TargetSocketOffset = SnipingSocketOffset;
	}
	else if (bIsAiming)
	{
		TargetArmLength = AimingTargetArmLength;
		TargetSocketOffset = AimingSocketOffset;
	}

	const bool bShouldLockRotation = true;
	bUseControllerRotationYaw = false;
	/*
	if (UCharacterMovementComponent* MovementComponent = GetCharacterMovement())
	{
		MovementComponent->bOrientRotationToMovement = false;
		MovementComponent->bUseControllerDesiredRotation = true;
	}
	*/

	if (CameraBoom)
	{
		CameraBoom->TargetArmLength = FMath::FInterpTo(CameraBoom->TargetArmLength, TargetArmLength, DeltaTime, CameraInterpSpeed);
		CameraBoom->SocketOffset = FMath::VInterpTo(CameraBoom->SocketOffset, TargetSocketOffset, DeltaTime, CameraInterpSpeed);
	}

	if (FollowCamera)
	{
		const float TargetFOV = bIsSniping ? SnipingFOV : DefaultFOV;
		FollowCamera->SetFieldOfView(FMath::FInterpTo(FollowCamera->FieldOfView, TargetFOV, DeltaTime, CameraInterpSpeed));
	}
}


void ABasePlayer::UpdateLocomotionStateSnapshot()
{
	if (!AnimStateComponent)
	{
		return;
	}

	FReplicatedLocomotionState NewSnapshot;
	NewSnapshot.bIsSprinting = AnimStateComponent->bIsSprinting;
	if (HasAuthority() && bHasAuthoritativeMoveInput)
	{
		NewSnapshot.MoveInput = AuthoritativeMoveInput.GetClampedToMaxSize(1.f);
		NewSnapshot.bHasMoveInput = NewSnapshot.MoveInput.SizeSquared() > FMath::Square(AnimStateComponent->MoveInputDeadZone);
	}
	else
	{
		NewSnapshot.bHasMoveInput = AnimStateComponent->bHasMoveInput;
		NewSnapshot.MoveInput = AnimStateComponent->CachedMoveInput.GetClampedToMaxSize(1.f);
	}

	// 네트워크 양자화 (소수점 첫째 자리까지만 유지하여 미세한 아날로그 스틱 떨림으로 인한 리플리케이션 폭주 방지)
	NewSnapshot.MoveInput.X = FMath::RoundHalfToEven(NewSnapshot.MoveInput.X * 10.f) / 10.f;
	NewSnapshot.MoveInput.Y = FMath::RoundHalfToEven(NewSnapshot.MoveInput.Y * 10.f) / 10.f;
	NewSnapshot.LandMoveDirection = AnimStateComponent->LandMoveDirection;
	NewSnapshot.LandStartGroundSpeed = AnimStateComponent->LandStartGroundSpeed;
	NewSnapshot.LastFallSpeed = AnimStateComponent->LastFallSpeed;
	NewSnapshot.EventSequence = LocomotionAnimEventSequence;
	NewSnapshot.LastLocomotionEvent = LocomotionStateSnapshot.LastLocomotionEvent;

	if (LocomotionStateSnapshot != NewSnapshot)
	{
		LocomotionStateSnapshot = NewSnapshot;
		// if (IsBasePlayerMotionMatchingCaptureEnabled())
		// {
		// 	const FString DebugLine = FString::Printf(
		// 		TEXT("[MMCAP_SNAPSHOT] Pawn=%s Net=%d Role=%d Seq=%d LastEvent=%d HasInput=%d MoveInput=(R=%.2f,F=%.2f) LandDir=(R=%.2f,F=%.2f) LandGround=%.1f Fall=%.1f Sprint=%d"),
		// 		*GetName(),
		// 		static_cast<int32>(GetNetMode()),
		// 		static_cast<int32>(GetLocalRole()),
		// 		NewSnapshot.EventSequence,
		// 		static_cast<int32>(NewSnapshot.LastLocomotionEvent),
		// 		NewSnapshot.bHasMoveInput ? 1 : 0,
		// 		NewSnapshot.MoveInput.X,
		// 		NewSnapshot.MoveInput.Y,
		// 		NewSnapshot.LandMoveDirection.X,
		// 		NewSnapshot.LandMoveDirection.Y,
		// 				NewSnapshot.LandStartGroundSpeed,
		// 				NewSnapshot.LastFallSpeed,
		// 				NewSnapshot.bIsSprinting ? 1 : 0);
		// 			// UE_LOG(LogTemp, Display, TEXT("%s"), *DebugLine);
		// 			AppendBasePlayerMotionMatchingCaptureLine(DebugLine);
		// 		}
	}
}

void ABasePlayer::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	UE_LOG(LogTemp, Log, TEXT("ABasePlayer::PossessedBy - [SERVER] Start. NewController: %s"), NewController ? *NewController->GetName() : TEXT("None"));

	// 서버 측 ASC 초기화 (InitAbilityActorInfo)
	ABasePlayerState* PS = GetPlayerState<ABasePlayerState>();
	if (PS)
	{
		UE_LOG(LogTemp, Log, TEXT("ABasePlayer::PossessedBy - [SERVER] PlayerState found: %s"), *PS->GetName());
		// Owner는 PlayerState, Avatar는 이 Character 객체로 설정
		PS->GetAbilitySystemComponent()->InitAbilityActorInfo(PS, this);

		// PlayerState로 부터 ASC 포인터 가져와서 캐싱
		CachedAbilitySystemComponent = PS->GetAbilitySystemComponent();
		if (HealthComponent)
		{
			HealthComponent->InitializeWithAbilitySystem(CachedAbilitySystemComponent.Get());
		}

		// Interact GA에 의해 발생한 Gameplay Event를 처리할 콜백 함수 등록
		// 현재는 Event 별로 따로 바인딩하지만 더 좋은 방법이 있을까?
		if(CachedAbilitySystemComponent.IsValid()) {
			CachedAbilitySystemComponent->GenericGameplayEventCallbacks.FindOrAdd(Interaction_PickUp).AddUObject(this, &ABasePlayer::HandlePickUpEvent);
			CachedAbilitySystemComponent->GenericGameplayEventCallbacks.FindOrAdd(Interaction_ShipBoard).AddUObject(this, &ABasePlayer::HandleShipBoardEvent);
			CachedAbilitySystemComponent->GenericGameplayEventCallbacks.FindOrAdd(Interaction_CannonBoard).AddUObject(this, &ABasePlayer::HandleCannonBoardEvent);

			UE_LOG(LogTemp, Log, TEXT("ABasePlayer::PossessedBy - [SERVER] DefaultGrantedAbilities Count: %d, DefaultAbilityMap Count: %d"),
				DefaultGrantedAbilities.Num(), DefaultAbilityMap.Num());

			// Map에 등록된 기본 어빌리티 순회 및 슬롯에 부여
			for (TSubclassOf<UGameplayAbility> AbilityClass : DefaultGrantedAbilities)
			{
				UE_LOG(LogTemp, Log, TEXT("ABasePlayer::PossessedBy - [SERVER] Granting Default Ability: %s"), AbilityClass ? *AbilityClass->GetName() : TEXT("None"));
				GrantDefaultAbility(AbilityClass);
			}

				for (const auto& AbilityPair : DefaultAbilityMap)
				{
				if (AbilityPair.Value)
				{
					UE_LOG(LogTemp, Log, TEXT("ABasePlayer::PossessedBy - [SERVER] Granting Ability: %s to Slot Tag: %s (InputID: %d)"),
						*AbilityPair.Value->GetName(),
						*AbilityPair.Key.ToString(),
						GetInputIDFromTag(AbilityPair.Key));
						GrantAbilityToSlot(AbilityPair.Key, AbilityPair.Value);
					}
				}
				if (bEnableGravityVortexTestInput && GravityVortexTestAbilityClass)
				{
					GrantAbilityToSlot(Key_Test_Skill_GravityVortex, GravityVortexTestAbilityClass);
				}
				if (bGrantWaterBombAbility && WaterBombAbilityClass)
				{
					GrantDefaultAbility(WaterBombAbilityClass);
				}
			}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("ABasePlayer::PossessedBy - [SERVER] CachedAbilitySystemComponent is invalid!"));
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("ABasePlayer::PossessedBy - [SERVER] PlayerState is null!"));
	}

	// ASC 초기화 완료 알림 방송
	OnAbilitySystemInitialized.Broadcast();
}

void ABasePlayer::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();

	// UE_LOG(LogTemp, Log, TEXT("ABasePlayer::OnRep_PlayerState - [CLIENT] Start."));

	// 클라이언트 측 ASC 초기화 (PlayerState가 클라로 복제되었음을 보장하는 타이밍)
	ABasePlayerState* PS = GetPlayerState<ABasePlayerState>();
	if (PS)
	{
		// UE_LOG(LogTemp, Log, TEXT("ABasePlayer::OnRep_PlayerState - [CLIENT] PlayerState found: %s"), *PS->GetName());
		// 클라이언트에서도 Owner와 Avatar를 연결해줌
		PS->GetAbilitySystemComponent()->InitAbilityActorInfo(PS, this);

		// 클라이언트 측 포인터 갱신
		CachedAbilitySystemComponent = PS->GetAbilitySystemComponent();
		if (HealthComponent)
		{
			HealthComponent->InitializeWithAbilitySystem(CachedAbilitySystemComponent.Get());
		}
		if (CachedAbilitySystemComponent.Get()) {
			//CachedAbilitySystemComponent->GenericGameplayEventCallbacks.FindOrAdd(Interaction_PickUp).AddUObject(this, &ABasePlayer::HandlePickUpEvent);
		}
	}
	else
	{
		// UE_LOG(LogTemp, Warning, TEXT("ABasePlayer::OnRep_PlayerState - [CLIENT] PlayerState is null!"));
	}

	OnAbilitySystemInitialized.Broadcast();
}

void ABasePlayer::PawnClientRestart()
{
	Super::PawnClientRestart();

	// 현재 폰을 조종하는 컨트롤러
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		// BasePlayer의 Item IMC
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
		{
			// DefaultIMC 등록
			if(DefaultIMC)
			{
				Subsystem->AddMappingContext(DefaultIMC, DefaultIMCPriority);
			}

			// ItemIMC 등록
			if (ItemIMC)
			{
				Subsystem->AddMappingContext(ItemIMC, ItemIMCPriority);
			}
		}

		// Crafter 전용 IMC
		if (UCrafterComponent* CrafterComp = FindComponentByClass<UCrafterComponent>())
		{
			CrafterComp->AddCrafterMappingContext();
		}

		// Interactable Object를 감지하기 위한 Trace 시작
		StartInteractionScan();
	}
}

void ABasePlayer::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		// CrafterComponent가 있다면 Crafter 전용 입력 바인딩
		if (UCrafterComponent* CrafterComp = FindComponentByClass<UCrafterComponent>())
		{
			CrafterComp->BindCrafterInput(EnhancedInputComponent);
		}

		// Jumping
		if (JumpAction)
		{
			EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Started, this, &ABasePlayer::DoJumpStart);
			EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Completed, this, &ABasePlayer::DoJumpEnd);
		}

		// Moving
		if (MoveAction)
		{
			EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ABasePlayer::Move);
			EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Completed, this, &ABasePlayer::MoveStopped);
			EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Canceled, this, &ABasePlayer::MoveStopped);
		}

		// Looking
		if (LookAction)
		{
			EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &ABasePlayer::Look);
		}
		if (MouseLookAction)
		{
			EnhancedInputComponent->BindAction(MouseLookAction, ETriggerEvent::Triggered, this, &ABasePlayer::Look);
		}

		if (SprintAction)
		{
			EnhancedInputComponent->BindAction(SprintAction, ETriggerEvent::Started, this, &ABasePlayer::StartSprint);
			EnhancedInputComponent->BindAction(SprintAction, ETriggerEvent::Triggered, this, &ABasePlayer::StartSprint);
			EnhancedInputComponent->BindAction(SprintAction, ETriggerEvent::Completed, this, &ABasePlayer::StopSprint);
			EnhancedInputComponent->BindAction(SprintAction, ETriggerEvent::Canceled, this, &ABasePlayer::StopSprint);
		}

		// Default 입력 바인딩
		if (DefaultInputConfig)
		{
			for (const FKeyInputAction& Action : DefaultInputConfig->KeyInputActions)
			{
				if (Action.InputAction && Action.KeyTag.IsValid())
				{
					// Key.Default.Mouse 태그 혹은 그 하위 태그인지 확인
					if (Action.KeyTag.MatchesTag(Key_Default_Mouse))
					{
						EnhancedInputComponent->BindAction(Action.InputAction, ETriggerEvent::Started, this, &ABasePlayer::OnMouseInputPressed, Action.KeyTag);
						EnhancedInputComponent->BindAction(Action.InputAction, ETriggerEvent::Completed, this, &ABasePlayer::OnMouseInputReleased, Action.KeyTag);
					}
					else
					{
						// 일반 키보드 입력
						EnhancedInputComponent->BindAction(Action.InputAction, ETriggerEvent::Started, this, &ABasePlayer::OnAbilityInputPressed, Action.KeyTag);
						EnhancedInputComponent->BindAction(Action.InputAction, ETriggerEvent::Completed, this, &ABasePlayer::OnAbilityInputReleased, Action.KeyTag);
					}
				}

			}
		}

	}

	PlayerInputComponent->BindKey(EKeys::One, IE_Pressed, this, &ABasePlayer::ActivateQuickSlot1);
	PlayerInputComponent->BindKey(EKeys::Two, IE_Pressed, this, &ABasePlayer::ActivateQuickSlot2);
	PlayerInputComponent->BindKey(EKeys::Three, IE_Pressed, this, &ABasePlayer::ActivateQuickSlot3);
	PlayerInputComponent->BindKey(EKeys::Four, IE_Pressed, this, &ABasePlayer::ActivateQuickSlot4);
	PlayerInputComponent->BindKey(EKeys::Five, IE_Pressed, this, &ABasePlayer::ActivateQuickSlot5);

	PlayerInputComponent->BindKey(EKeys::LeftShift, IE_Pressed, this, &ABasePlayer::StartSprint);
	PlayerInputComponent->BindKey(EKeys::LeftShift, IE_Released, this, &ABasePlayer::StopSprint);
	PlayerInputComponent->BindKey(EKeys::RightShift, IE_Pressed, this, &ABasePlayer::StartSprint);
	PlayerInputComponent->BindKey(EKeys::RightShift, IE_Released, this, &ABasePlayer::StopSprint);
	if (bEnableGravityVortexTestInput)
	{
		PlayerInputComponent->BindKey(EKeys::Three, IE_Pressed, this, &ABasePlayer::OnGravityVortexTestPressed);
		PlayerInputComponent->BindKey(EKeys::Three, IE_Released, this, &ABasePlayer::OnGravityVortexTestReleased);
	}
}

int32 ABasePlayer::GetInputIDFromTag(const FGameplayTag& Tag) const
{
	if (!Tag.IsValid()) return INDEX_NONE;
	return static_cast<int32>(FCrc::StrCrc32(*Tag.ToString()));
}

void ABasePlayer::InitializeQuickSlots()
{
	if (!HasAuthority() || QuickSlots.Num() == 5)
	{
		return;
	}

	QuickSlots.Reset(5);
	const FGameplayTag SlotTags[] = { Key_Item_1, Key_Item_2, Key_Item_3, Key_Item_4, Key_Item_5 };
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(SlotTags); ++Index)
	{
		FQuickSlotReference& Slot = QuickSlots.AddDefaulted_GetRef();
		Slot.KeyTag = SlotTags[Index];
		Slot.SlotType = Index < 2 ? EQuickSlotType::Weapon : EQuickSlotType::Consumable;
	}
}

bool ABasePlayer::CanQuickSlotAcceptItem(int32 QuickSlotIndex, FGameplayTag ItemTag) const
{
	if (!QuickSlots.IsValidIndex(QuickSlotIndex) || !ItemTag.IsValid())
	{
		return false;
	}

	const UWorld* World = GetWorld();
	const UItemSubsystem* ItemSubsystem = World ? World->GetSubsystem<UItemSubsystem>() : nullptr;
	if (!ItemSubsystem)
	{
		return false;
	}

	const FGameplayTag CategoryTag = ItemSubsystem->GetCategoryTag(ItemTag);
	const bool bIsWeapon = CategoryTag.MatchesTag(Item_Category_Weapon)
		|| ItemTag.MatchesTag(Item_Id_Weapon);
	return QuickSlots[QuickSlotIndex].SlotType == EQuickSlotType::Weapon
		? bIsWeapon
		: CategoryTag.MatchesTag(Item_Category_Consumable);
}

void ABasePlayer::AssignQuickSlotFromInventory(int32 QuickSlotIndex)
{
	if (!HasAuthority())
	{
		ServerAssignQuickSlotFromInventory(QuickSlotIndex);
		return;
	}

	if (!InventoryComponent || !QuickSlots.IsValidIndex(QuickSlotIndex))
	{
		return;
	}

	const FInventoryCursorItem& CursorItem = InventoryComponent->GetCursorItem();
	if (!CursorItem.IsValid() || !CanQuickSlotAcceptItem(QuickSlotIndex, CursorItem.ItemTag))
	{
		return;
	}

	const FGameplayTag AssignedItemTag = CursorItem.ItemTag;
	for (FQuickSlotReference& Slot : QuickSlots)
	{
		if (Slot.ItemTag == AssignedItemTag)
		{
			Slot.ItemTag = FGameplayTag();
		}
	}

	QuickSlots[QuickSlotIndex].ItemTag = AssignedItemTag;
	InventoryComponent->ReturnCursorToOriginalSlot();
	OnQuickSlotsChanged.Broadcast();
}

void ABasePlayer::ServerAssignQuickSlotFromInventory_Implementation(int32 QuickSlotIndex)
{
	AssignQuickSlotFromInventory(QuickSlotIndex);
}

void ABasePlayer::ClearQuickSlot(int32 QuickSlotIndex)
{
	if (!HasAuthority())
	{
		ServerClearQuickSlot(QuickSlotIndex);
		return;
	}

	if (QuickSlots.IsValidIndex(QuickSlotIndex) && !QuickSlots[QuickSlotIndex].IsEmpty())
	{
		QuickSlots[QuickSlotIndex].ItemTag = FGameplayTag();
		OnQuickSlotsChanged.Broadcast();
	}
}

void ABasePlayer::ServerClearQuickSlot_Implementation(int32 QuickSlotIndex)
{
	ClearQuickSlot(QuickSlotIndex);
}

void ABasePlayer::ActivateQuickSlot1() { ActivateQuickSlot(0); }
void ABasePlayer::ActivateQuickSlot2() { ActivateQuickSlot(1); }
void ABasePlayer::ActivateQuickSlot3() { ActivateQuickSlot(2); }
void ABasePlayer::ActivateQuickSlot4() { ActivateQuickSlot(3); }
void ABasePlayer::ActivateQuickSlot5() { ActivateQuickSlot(4); }

void ABasePlayer::ActivateQuickSlot(int32 QuickSlotIndex)
{
	if (!HasAuthority())
	{
		ServerActivateQuickSlot(QuickSlotIndex);
		return;
	}

	if (!QuickSlots.IsValidIndex(QuickSlotIndex))
	{
		return;
	}

	const FQuickSlotReference& Slot = QuickSlots[QuickSlotIndex];
	if (Slot.IsEmpty())
	{
		UnequipCurrentItem();
		return;
	}

	if (!InventoryComponent || InventoryComponent->GetMaterialCount(Slot.ItemTag) <= 0)
	{
		ClearQuickSlot(QuickSlotIndex);
		UnequipCurrentItem();
		return;
	}

	if (Slot.SlotType == EQuickSlotType::Weapon)
	{
		EquipInventoryWeapon(Slot.ItemTag);
	}
	else
	{
		ConsumeInventoryItem(Slot.ItemTag);
	}
}

void ABasePlayer::ServerActivateQuickSlot_Implementation(int32 QuickSlotIndex)
{
	ActivateQuickSlot(QuickSlotIndex);
}

bool ABasePlayer::IsEquippedItemOwnedByLegacySlot() const
{
	return IsValid(EquippedItem) && ItemSlots.ContainsByPredicate([this](const FItemSlot& Slot)
	{
		return Slot.Item == EquippedItem;
	});
}

void ABasePlayer::UnequipCurrentItem()
{
	if (!HasAuthority() || !IsValid(EquippedItem))
	{
		return;
	}

	FGameplayTag UseKeyTag = Key_Default_Mouse_LeftClick;
	if (UItemSubsystem* ItemSubsystem = GetWorld() ? GetWorld()->GetSubsystem<UItemSubsystem>() : nullptr)
	{
		const FGameplayTag ConfiguredUseKey = ItemSubsystem->GetUseKeyTag(EquippedItem->ItemTag);
		if (ConfiguredUseKey.IsValid())
		{
			UseKeyTag = ConfiguredUseKey;
		}
	}
	RemoveAbilityFromSlot(UseKeyTag);

	ABaseItem* PreviousItem = EquippedItem;
	const bool bLegacySlotItem = IsEquippedItemOwnedByLegacySlot();
	EquippedItem = nullptr;
	if (bLegacySlotItem)
	{
		PreviousItem->SetItemState(EItemState::InItemSlot);
	}
	else
	{
		PreviousItem->Destroy();
	}

	OnItemSlotsChanged.Broadcast();
	OnQuickSlotsChanged.Broadcast();
}

bool ABasePlayer::EquipInventoryWeapon(FGameplayTag ItemTag)
{
	if (!HasAuthority() || !InventoryComponent || InventoryComponent->GetMaterialCount(ItemTag) <= 0)
	{
		return false;
	}

	if (IsValid(EquippedItem) && EquippedItem->ItemTag == ItemTag && !IsEquippedItemOwnedByLegacySlot())
	{
		return true;
	}

	UnequipCurrentItem();
	UItemSubsystem* ItemSubsystem = GetWorld() ? GetWorld()->GetSubsystem<UItemSubsystem>() : nullptr;
	if (!ItemSubsystem)
	{
		return false;
	}

	ABaseItem* SpawnedItem = ItemSubsystem->SpawnItem(ItemTag, GetActorTransform(), EItemState::InItemSlot, this);
	if (!IsValid(SpawnedItem))
	{
		return false;
	}

	EquippedItem = SpawnedItem;
	EquippedItem->SetItemState(EItemState::Equipped);
	EquippedItem->AttachToComponent(GetMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale,
		ItemSubsystem->GetAttachmentSocketName(ItemTag));

	bool bCanUse = EquippedItem->GetCanUseAbilityList().IsEmpty();
	if (!bCanUse && CachedAbilitySystemComponent.IsValid())
	{
		for (const FGameplayTag& RequiredTag : EquippedItem->GetCanUseAbilityList())
		{
			if (CachedAbilitySystemComponent->HasMatchingGameplayTag(RequiredTag))
			{
				bCanUse = true;
				break;
			}
		}
	}

	if (bCanUse)
	{
		if (TSubclassOf<UGameplayAbility> AbilityClass = EquippedItem->GetGrantedAbilityClass())
		{
			FGameplayTag UseKeyTag = ItemSubsystem->GetUseKeyTag(ItemTag);
			GrantAbilityToSlot(UseKeyTag.IsValid() ? UseKeyTag : Key_Default_Mouse_LeftClick, AbilityClass);
		}
	}

	OnItemSlotsChanged.Broadcast();
	OnQuickSlotsChanged.Broadcast();
	return true;
}

bool ABasePlayer::ConsumeInventoryItem(FGameplayTag ItemTag)
{
	if (!HasAuthority() || !InventoryComponent || !CachedAbilitySystemComponent.IsValid())
	{
		return false;
	}

	UItemSubsystem* ItemSubsystem = GetWorld() ? GetWorld()->GetSubsystem<UItemSubsystem>() : nullptr;
	if (!ItemSubsystem || !ItemSubsystem->GetCategoryTag(ItemTag).MatchesTag(Item_Category_Consumable))
	{
		return false;
	}

	TSubclassOf<UGameplayAbility> AbilityClass = ItemSubsystem->GetGrantedAbilityClass(ItemTag).LoadSynchronous();
	if (!AbilityClass)
	{
		return false;
	}

	FGameplayAbilitySpec AbilitySpec(AbilityClass, 1, INDEX_NONE, this);
	const FGameplayAbilitySpecHandle Handle = CachedAbilitySystemComponent->GiveAbilityAndActivateOnce(AbilitySpec);
	if (!Handle.IsValid())
	{
		return false;
	}

	return InventoryComponent->RemoveMaterial(ItemTag, 1);
}

void ABasePlayer::HandleInventoryContentsChanged()
{
	if (!HasAuthority() || !InventoryComponent)
	{
		return;
	}

	const FInventoryCursorItem& CursorItem = InventoryComponent->GetCursorItem();
	bool bChanged = false;
	for (FQuickSlotReference& Slot : QuickSlots)
	{
		const bool bHeldByCursor = CursorItem.IsValid() && CursorItem.ItemTag == Slot.ItemTag;
		if (!Slot.IsEmpty() && !bHeldByCursor && InventoryComponent->GetMaterialCount(Slot.ItemTag) <= 0)
		{
			Slot.ItemTag = FGameplayTag();
			bChanged = true;
		}
	}

	if (IsValid(EquippedItem) && !IsEquippedItemOwnedByLegacySlot())
	{
		const bool bHeldByCursor = CursorItem.IsValid() && CursorItem.ItemTag == EquippedItem->ItemTag;
		if (!bHeldByCursor && InventoryComponent->GetMaterialCount(EquippedItem->ItemTag) <= 0)
		{
			UnequipCurrentItem();
		}
	}

	if (bChanged)
	{
		OnQuickSlotsChanged.Broadcast();
	}
}

bool ABasePlayer::TryPutItemInSlot(ABaseItem* Item)
{
	if (!IsValid(Item)) return false;

	// 빈 ItemSlot Index 찾기
	int32 EmptySlotIndex = ItemSlots.IndexOfByPredicate([](const FItemSlot& Slot)
		{
			return !IsValid(Slot.Item);
		});

	if (EmptySlotIndex != INDEX_NONE)
	{
		// 빈 슬롯에 저장
		ItemSlots[EmptySlotIndex].Item = Item;

		OnItemSlotsChanged.Broadcast();
		
		if (IsValid(EquippedItem))
		{
			// 이미 손에 무언가 들려있으면 새로 주운 아이템은 보이지 않게
			Item->SetItemState(EItemState::InItemSlot);
		}
		else
		{
			// 손이 비어있으면 새로 주운 아이템 바로 장착
			EquipItemFromSlot(ItemSlots[EmptySlotIndex].KeyTag);
		}
		return true; // 성공적으로 슬롯에 넣음
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("ABasePlayer::TryPutItemInSlot : ItemSlot is Full."));
		return false; // 아이템 슬롯 꽉 참
	}
}

void ABasePlayer::GrantAbilityToSlot(FGameplayTag KeyTag, TSubclassOf<UGameplayAbility> AbilityClass)
{
	// 서버에서만 실행되며, 유효성 검사 수행
	if (!HasAuthority() || !CachedAbilitySystemComponent.Get() || !AbilityClass || !KeyTag.IsValid())
	{
		return;
	}

	int32 TargetInputID = GetInputIDFromTag(KeyTag);
	if (TargetInputID != INDEX_NONE)
	{
		// 이미 해당 InputID에 동일한 클래스의 어빌리티가 부여되어 있는지 확인
		for (const FGameplayAbilitySpec& Spec : CachedAbilitySystemComponent->GetActivatableAbilities())
		{
			if (Spec.InputID == TargetInputID && Spec.Ability && Spec.Ability->GetClass() == AbilityClass)
			{
				// 이미 동일한 어빌리티가 동일 슬롯에 존재하므로 중복 부여하지 않고 리턴
				return;
			}
		}
	}

	// 해당 슬롯에 다른 어빌리티가 있거나 없을 때만 제거 후 부여
	RemoveAbilityFromSlot(KeyTag);

	// 통합 맵에서 이 태그에 할당된 ID를 가져옴
	int32 AssignedID = TargetInputID;

	// GA Spec 생성 시 해당 ID 주입
	FGameplayAbilitySpec Spec(AbilityClass, 1, AssignedID, this);
	CachedAbilitySystemComponent->GiveAbility(Spec);
}

void ABasePlayer::GrantDefaultAbility(TSubclassOf<UGameplayAbility> AbilityClass)
{
	if (!HasAuthority() || !CachedAbilitySystemComponent.Get() || !AbilityClass)
	{
		return;
	}

	for (const FGameplayAbilitySpec& Spec : CachedAbilitySystemComponent->GetActivatableAbilities())
	{
		if (Spec.Ability && Spec.Ability->GetClass() == AbilityClass)
		{
			return;
		}
	}

	FGameplayAbilitySpec Spec(AbilityClass, 1, INDEX_NONE, this);
	CachedAbilitySystemComponent->GiveAbility(Spec);
}

void ABasePlayer::RemoveAbilityFromSlot(FGameplayTag KeyTag)
{
	if (!HasAuthority() || !CachedAbilitySystemComponent.Get() || !KeyTag.IsValid())
	{
		return;
	}

	int32 TargetInputID = GetInputIDFromTag(KeyTag);
	if (TargetInputID == INDEX_NONE) return;

	// GAS 내부의 부여된 어빌리티 목록을 순회하며 매핑된 InputID를 가진 어빌리티 수집 및 제거
	TArray<FGameplayAbilitySpecHandle> HandlesToRemove;
	for (const FGameplayAbilitySpec& Spec : CachedAbilitySystemComponent.Get()->GetActivatableAbilities())
	{
		// Spec.InputID가 우리가 제거하려는 ID와 일치한다면
		if (Spec.InputID == TargetInputID)
		{
			HandlesToRemove.Add(Spec.Handle);
		}
	}

	for (const FGameplayAbilitySpecHandle& Handle : HandlesToRemove)
	{
		UE_LOG(LogTemp, Log, TEXT("ABasePlayer::RemoveAbilityFromSlot : Removing ability with KeyTag: %s"), *KeyTag.ToString());
		CachedAbilitySystemComponent->ClearAbility(Handle);
	}
}

void ABasePlayer::OnAbilityInputPressed(FGameplayTag InputTag)
{
	if (!CachedAbilitySystemComponent.Get() || !InputTag.IsValid())
	{
		// UE_LOG(LogTemp, Warning, TEXT("ABasePlayer::OnAbilityInputPressed - [%s] Fails: CachedAbilitySystemComponent valid? %s, InputTag: %s"),
		// 	HasAuthority() ? TEXT("SERVER") : TEXT("CLIENT"),
		// 	CachedAbilitySystemComponent.IsValid() ? TEXT("YES") : TEXT("NO"),
		// 	*InputTag.ToString());
		return;
	}

	if (IsEquipmentTransitioning())
	{
		return;
	}

	int32 InputID = GetInputIDFromTag(InputTag);
	// UE_LOG(LogTemp, Log, TEXT("ABasePlayer::OnAbilityInputPressed - [%s] KeyTag: %s, InputID: %d, LocallyControlled: %s"),
	// 	HasAuthority() ? TEXT("SERVER") : TEXT("CLIENT"),
	// 	*InputTag.ToString(),
	// 	InputID,
	// 	IsLocallyControlled() ? TEXT("YES") : TEXT("NO"));

	// 현재 부여된 모든 어빌리티 및 그 InputID 출력
	const TArray<FGameplayAbilitySpec>& Specs = CachedAbilitySystemComponent->GetActivatableAbilities();
	// UE_LOG(LogTemp, Log, TEXT("ABasePlayer::OnAbilityInputPressed - [%s] Activatable Abilities Count: %d"), 
	// 	HasAuthority() ? TEXT("SERVER") : TEXT("CLIENT"), Specs.Num());
	for (const FGameplayAbilitySpec& Spec : Specs)
	{
		// UE_LOG(LogTemp, Log, TEXT("  - Ability: %s, InputID: %d, Active: %s"), 
		// 	Spec.Ability ? *Spec.Ability->GetName() : TEXT("None"),
		// 	Spec.InputID,
		// 	Spec.IsActive() ? TEXT("YES") : TEXT("NO"));
	}

	if (InputID != INDEX_NONE)
	{
		CachedAbilitySystemComponent->AbilityLocalInputPressed(InputID);
	}
}

void ABasePlayer::OnAbilityInputReleased(FGameplayTag InputTag)
{
	if (!CachedAbilitySystemComponent.Get() || !InputTag.IsValid()) return;

	int32 InputID = GetInputIDFromTag(InputTag);
	// UE_LOG(LogTemp, Log, TEXT("ABasePlayer::OnAbilityInputReleased - [%s] KeyTag: %s, InputID: %d, LocallyControlled: %s"),
	// 	HasAuthority() ? TEXT("SERVER") : TEXT("CLIENT"),
	// 	*InputTag.ToString(),
	// 	InputID,
	// 	IsLocallyControlled() ? TEXT("YES") : TEXT("NO"));

	if (InputID != INDEX_NONE)
	{
		CachedAbilitySystemComponent->AbilityLocalInputReleased(InputID);
	}
}

void ABasePlayer::OnGravityVortexTestPressed()
{
	if (bEnableGravityVortexTestInput)
	{
		OnAbilityInputPressed(Key_Test_Skill_GravityVortex);
	}
}

void ABasePlayer::OnGravityVortexTestReleased()
{
	if (bEnableGravityVortexTestInput)
	{
		OnAbilityInputReleased(Key_Test_Skill_GravityVortex);
	}
}

void ABasePlayer::OnMouseInputPressed(FGameplayTag InputTag)
{
	if (!CachedAbilitySystemComponent.Get() || !InputTag.IsValid()) return;
	if (IsEquipmentTransitioning()) return;

	// 공통 GAS 입력 해제 처리
	const bool bGravityVortexOwnsLeftClick =
		InputTag.MatchesTagExact(Key_Default_Mouse_LeftClick)
		&& CachedAbilitySystemComponent->HasMatchingGameplayTag(GameplayAbility_Skill_GravityVortex);
	if (!bGravityVortexOwnsLeftClick)
	{
		OnAbilityInputPressed(InputTag);
	}

	// ASC에 GameplayEvent로서 전달
	FGameplayEventData EventData;
	EventData.Instigator = this;
	EventData.Target = nullptr;

	CachedAbilitySystemComponent->HandleGameplayEvent(InputTag, &EventData);

	if (!HasAuthority())
	{
		ServerRPC_SendGameplayEvent(InputTag, EventData);
	}
}

void ABasePlayer::OnMouseInputReleased(FGameplayTag InputTag)
{
	if (!CachedAbilitySystemComponent.Get() || !InputTag.IsValid()) return;

	// 공통 GAS 입력 해제 처리
	OnAbilityInputReleased(InputTag);

	// Release 이벤트를 위한 태그 맵핑
	FGameplayTag ReleasedEventTag = InputTag;
	if (InputTag.MatchesTagExact(Key_Default_Mouse_LeftClick))
	{
		ReleasedEventTag = Key_Default_Mouse_LeftClick_Released;
	}
	else if (InputTag.MatchesTagExact(Key_Default_Mouse_RightClick))
	{
		ReleasedEventTag = Key_Default_Mouse_RightClick_Released;
	}

	// ASC에 GameplayEvent로서 전달
	FGameplayEventData EventData;
	EventData.Instigator = this;
	EventData.Target = nullptr;
	if (ReleasedEventTag.MatchesTagExact(Key_Default_Mouse_LeftClick_Released))
	{
		AddMouseAimTargetData(EventData);
	}

	CachedAbilitySystemComponent->HandleGameplayEvent(ReleasedEventTag, &EventData);

	if (!HasAuthority())
	{
		ServerRPC_SendGameplayEvent(ReleasedEventTag, EventData);
	}
}

void ABasePlayer::AddMouseAimTargetData(FGameplayEventData& EventData) const
{
	const ABowItem* Bow = Cast<ABowItem>(EquippedItem);
	const UBowComponent* BowComponent = Bow ? Bow->GetBowComponent() : nullptr;
	if (!BowComponent)
	{
		return;
	}

	const FVector ViewLocation = FollowCamera ? FollowCamera->GetComponentLocation() : GetActorLocation();
	const FVector ViewForward = FollowCamera ? FollowCamera->GetForwardVector() : GetBaseAimRotation().Vector();

	TArray<AActor*> ActorsToIgnore;
	ActorsToIgnore.Add(const_cast<ABasePlayer*>(this));
	if (EquippedItem)
	{
		ActorsToIgnore.Add(EquippedItem);
	}

	FBowAimResult AimResult;
	if (!BowComponent->CalculateAim(ViewLocation, ViewForward, ActorsToIgnore, AimResult))
	{
		return;
	}

	FHitResult AimHit;
	AimHit.TraceStart = AimResult.TraceStart;
	AimHit.TraceEnd = AimResult.TraceEnd;
	AimHit.Location = AimResult.AimTarget;
	AimHit.ImpactPoint = AimResult.AimTarget;
	AimHit.bBlockingHit = AimResult.bBlockingHit;

	FGameplayAbilityTargetData_SingleTargetHit* TargetData = new FGameplayAbilityTargetData_SingleTargetHit(AimHit);
	EventData.TargetData.Add(TargetData);
}

void ABasePlayer::HandleShipBoardEvent(const FGameplayEventData* Payload)
{
	UE_LOG(LogTemp, Log, TEXT("ABasePlayer::HandleShipBoardEvent - [%s] Event received. Payload valid: %s, Target: %s"),
		HasAuthority() ? TEXT("SERVER") : TEXT("CLIENT"),
		Payload ? TEXT("YES") : TEXT("NO"),
		Payload && Payload->Target ? *Payload->Target->GetName() : TEXT("None"));

	if (Payload && Payload->Target)
	{
		if (AShip* TargetShip = const_cast<AShip*>(Cast<AShip>(Payload->Target)))
		{
			TargetShip->Board(this);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("ABasePlayer::HandleShipBoardEvent - [%s] Target is not AShip! Target: %s"),
				HasAuthority() ? TEXT("SERVER") : TEXT("CLIENT"),
				*Payload->Target->GetName());
		}
	}
}

void ABasePlayer::HandleCannonBoardEvent(const FGameplayEventData* Payload)
{
	UE_LOG(LogTemp, Log, TEXT("ABasePlayer::HandleCannonBoardEvent - [%s] Event received. Payload valid: %s, Target: %s"),
		HasAuthority() ? TEXT("SERVER") : TEXT("CLIENT"),
		Payload ? TEXT("YES") : TEXT("NO"),
		Payload && Payload->Target ? *Payload->Target->GetName() : TEXT("None"));

	if (Payload && Payload->Target)
	{
		if (ACannon* TargetCannon = const_cast<ACannon*>(Cast<ACannon>(Payload->Target)))
		{
			TargetCannon->Board(this);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("ABasePlayer::HandleCannonBoardEvent - [%s] Target is not ACannon! Target: %s"),
				HasAuthority() ? TEXT("SERVER") : TEXT("CLIENT"),
				*Payload->Target->GetName());
		}
	}
}

void ABasePlayer::HandlePickUpEvent(const FGameplayEventData* Payload)
{
	if (Payload && Payload->Target)
	{
		if (ABaseItem* ItemToPickUp = const_cast<ABaseItem*>(Cast<ABaseItem>(Payload->Target)))
		{
			// 아이템 태그가 material 로 시작하면 인벤토리로
			bool bShouldStoreInInventory = ItemToPickUp->ItemTag.MatchesTag(Item_Material);
			if (UWorld* World = GetWorld())
			{
				if (UItemSubsystem* ItemSubsystem = World->GetSubsystem<UItemSubsystem>())
				{
					const FGameplayTag CategoryTag = ItemSubsystem->GetCategoryTag(ItemToPickUp->ItemTag);
					bShouldStoreInInventory =
						bShouldStoreInInventory ||
						CategoryTag.MatchesTag(Item_Category_Clue) ||
						CategoryTag.MatchesTag(Item_Category_Consumable) ||
						CategoryTag.MatchesTag(Item_Category_Material) ||
						CategoryTag.MatchesTag(Item_Category_Weapon);
				}
			}

			if (bShouldStoreInInventory)
			{
				if (InventoryComponent && InventoryComponent ->AddMaterial(ItemToPickUp->ItemTag, 1))
				{
					ItemToPickUp->Destroy();
				}
				return;
			}

			// 장착형 아이템 처리 로직 (서버에서만 생성/파괴 수행)
			if (HasAuthority())
			{
				// 슬롯 여유 공간 확인 (불필요한 힙 메모리 할당 및 스폰 연산 방지)
				if (HasEmptyItemSlot())
				{
					UWorld* World = GetWorld();
					if (IsValid(World))
					{
						if (UItemSubsystem* ItemSubsystem = World->GetSubsystem<UItemSubsystem>())
						{
							// 기존 아이템의 데이터 캐싱 (상수화로 불변성 보장)
							const FGameplayTag TargetItemTag = ItemToPickUp->ItemTag;
							const FTransform SpawnTransform = ItemToPickUp->GetActorTransform();

							// 서브시스템을 통해 새로운 아이템 스폰 (초기 상태를 InItemSlot으로 지정)
							ABaseItem* NewSpawnedItem = ItemSubsystem->SpawnItem(TargetItemTag, SpawnTransform, EItemState::InItemSlot, this);

							if (IsValid(NewSpawnedItem))
							{
								// 성공적으로 스폰되었다면 슬롯에 할당 시도
								if (TryPutItemInSlot(NewSpawnedItem))
								{
									// 슬롯 등록까지 완료되었을 때만 기존 바닥의 아이템을 맵에서 제거
									ItemToPickUp->Destroy();
								}
								else
								{
									// 동시성 문제 등으로 슬롯 등록이 실패했다면 고아(Orphan) 액터가 되지 않도록 롤백
									NewSpawnedItem->Destroy();
									UE_LOG(LogTemp, Warning, TEXT("ABasePlayer::HandlePickUpEvent : Failed to put new item in slot. Spawn rolled back."));
								}
							}
						}
					}
				}
				else
				{
					UE_LOG(LogTemp, Warning, TEXT("ABasePlayer::HandlePickUpEvent : Inventory is full. Cannot pick up %s"), *ItemToPickUp->GetName());
				}
			}
		}
	}
}

void ABasePlayer::UseEquippedItem(bool bDestroy)
{
	if (EquipmentComponent)
	{
		EquipmentComponent->UseEquippedItem(bDestroy);
	}
}

void ABasePlayer::EquipItemFromSlot(FGameplayTag KeyTag)
{
	if (bEnableGravityVortexTestInput && KeyTag.MatchesTagExact(Key_Item_3))
	{
		return;
	}

	if (EquipmentComponent)
	{
		EquipmentComponent->EquipItemFromSlot(KeyTag);
	}
}

void ABasePlayer::Server_EquipItemFromSlot_Implementation(FGameplayTag KeyTag)
{
	// 서버가 다시 본래의 함수를 호출하여 권한(HasAuthority)을 통과시키고 실제 로직을 실행
	EquipItemFromSlot(KeyTag);
}

EEquipmentState ABasePlayer::GetEquipmentState() const
{
	return EquipmentComponent ? EquipmentComponent->GetEquipmentState() : EEquipmentState::None;
}

bool ABasePlayer::IsEquipmentTransitioning() const
{
	return EquipmentComponent && EquipmentComponent->IsEquipmentTransitioning();
}

void ABasePlayer::HandleEquipmentAttachNotify()
{
	if (EquipmentComponent)
	{
		EquipmentComponent->HandleEquipmentAttachNotify();
	}
}

void ABasePlayer::RemoveItemFromSlot(FGameplayTag KeyTag)
{
	// [서버]
	if (!HasAuthority()) return;
	int32 SlotIndex = ItemSlots.IndexOfByKey(KeyTag);
	if (ItemSlots.IsValidIndex(SlotIndex) && IsValid(ItemSlots[SlotIndex].Item))
	{
		ItemSlots[SlotIndex].Item = nullptr;
		RemoveAbilityFromSlot(KeyTag);
		OnItemSlotsChanged.Broadcast();
	}
}

bool ABasePlayer::HasEmptyItemSlot() const
{
	// 람다를 사용해 비어있는(Invalid한) 아이템 포인터가 하나라도 있는지 검사
	return ItemSlots.ContainsByPredicate([](const FItemSlot& Slot)
		{
			return !IsValid(Slot.Item);
		});
}

void ABasePlayer::ServerRPC_SendGameplayEvent_Implementation(FGameplayTag EventTag, FGameplayEventData Payload)
{
	// 서버의 ASC에서 이벤트를 발생시켜 WaitGameplayEvent 태스크를 깨웁니다.
	UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(this, EventTag, Payload);
}

void ABasePlayer::OnRep_EquippedItem()
{
	if (EquipmentComponent)
	{
		EquipmentComponent->OnRepOwnerEquippedItem();
	}

	OnItemSlotsChanged.Broadcast();
}

void ABasePlayer::StartInteractionScan()
{
	// [클라/로컬]
	if (IsLocallyControlled())
	{
		// 설정값이 0 이하일 경우를 대비한 방어 코드
		float ScanInterval = InteractionScanInterval > 0.f ? InteractionScanInterval : 0.1f;

		GetWorldTimerManager().SetTimer(
			InteractionScanTimerHandle,
			this,
			&ABasePlayer::PerformInteractionScan,
			ScanInterval,
			true // 반복 실행(Loop)
		);
	}
}

bool ABasePlayer::PerformInteractTrace(TArray<FHitResult>& OutHitResults) const
{
	OutHitResults.Empty();

	FVector StartLoc = GetActorLocation();
	FVector EndLoc = StartLoc + (GetActorForwardVector() * InteractTraceDistance);

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this); // 자기 자신 스캔 제외

	TArray<FHitResult> HitResults;

	bool bHit = GetWorld()->SweepMultiByChannel(
		OutHitResults,
		StartLoc,
		EndLoc,
		FQuat::Identity,
		ECC_Interactable,
		FCollisionShape::MakeSphere(InteractTraceRadius),
		QueryParams
	);

#if ENABLE_DRAW_DEBUG
#if WITH_EDITOR
	if (GIsEditor)
	{
		FColor DrawColor = bHit ? FColor::Green : FColor::Red;
		FVector TraceCenter = StartLoc + (EndLoc - StartLoc) * 0.5f;
		float TraceHalfHeight = (EndLoc - StartLoc).Size() * 0.5f;
		FQuat TraceRotation = FRotationMatrix::MakeFromZ(EndLoc - StartLoc).ToQuat();

		// 타이머 주기에 맞춰 그려지도록 LifeTime을 짧게 설정 (예: 0.1초)
		// DrawDebugCapsule(GetWorld(), TraceCenter, TraceHalfHeight, InteractTraceRadius, TraceRotation, DrawColor, false, 0.1f);
	}
#endif
#endif

	return bHit;
}

void ABasePlayer::PerformInteractionScan()
{
	TArray<FHitResult> HitResults;
	PerformInteractTrace(HitResults);

	TArray<UWidgetComponent*> CurrentHoveredWidgets;

	// 현재 트레이스에 걸린 모든 위젯 수집
	for (const FHitResult& Hit : HitResults)
	{
		if (AActor* HitActor = Hit.GetActor())
		{
			if (!HitActor->FindComponentByClass<UInteractableComponent>())
			{
				continue;
			}

			TArray<UWidgetComponent*> WidgetComponents;
			HitActor->GetComponents<UWidgetComponent>(WidgetComponents);
			for (UWidgetComponent* WidgetComp : WidgetComponents)
			{
				if (!WidgetComp)
				{
					continue;
				}

				if (Cast<UInteractUserWidget>(WidgetComp->GetUserWidgetObject()))
				{
					CurrentHoveredWidgets.AddUnique(WidgetComp);
				}
			}
		}
	}

	// 기존 캐시에는 있지만 현재 스캔되지 않은 위젯은 숨김 처리 후 캐시에서 제거
	for (int32 i = CachedHoveredWidgets.Num() - 1; i >= 0; --i)
	{
		if (CachedHoveredWidgets[i].IsValid())
		{
			UWidgetComponent* CachedWidget = CachedHoveredWidgets[i].Get();
			if (!CurrentHoveredWidgets.Contains(CachedWidget))
			{
				CachedWidget->SetHiddenInGame(true);
				CachedHoveredWidgets.RemoveAt(i);
			}
		}
		else
		{
			// 유효하지 않은 포인터 정리
			CachedHoveredWidgets.RemoveAt(i);
		}
	}

	// 새로 스캔된 위젯 표시 및 캐시에 등록
	for (UWidgetComponent* Widget : CurrentHoveredWidgets)
	{
		if (Widget)
		{
			bool bAlreadyCached = false;
			for (const auto& Cached : CachedHoveredWidgets)
			{
				if (Cached.Get() == Widget)
				{
					bAlreadyCached = true;
					break;
				}
			}

			if (!bAlreadyCached)
			{
				Widget->SetHiddenInGame(false);
				CachedHoveredWidgets.Add(Widget);

				if (AActor* OwnerActor = Widget->GetOwner())
				{
					// InteractableComponent
					if (UInteractableComponent* InteractComp = OwnerActor->FindComponentByClass<UInteractableComponent>())
					{
						// InteractUserWidget으로 캐스팅
						if (UInteractUserWidget* InteractWidget = Cast<UInteractUserWidget>(Widget->GetUserWidgetObject()))
						{
							// BP에서 구현된 UI 업데이트 함수 호출
							InteractWidget->OnUpdateInteractUI(InteractComp->InteractUIInfo);
						}
					}
				}
			}
		}
	}
}

void ABasePlayer::Move(const FInputActionValue& Value)
{
	FVector2D MovementVector = Value.Get<FVector2D>();
	DoMove(MovementVector.X, MovementVector.Y);
}

void ABasePlayer::MoveStopped(const FInputActionValue& Value)
{
	StopMoveInput();
}

void ABasePlayer::Look(const FInputActionValue& Value)
{
	FVector2D LookAxisVector = Value.Get<FVector2D>();
	DoLook(LookAxisVector.X, LookAxisVector.Y);
}

void ABasePlayer::DoMove(float Right, float Forward)
{
	if(AnimStateComponent) AnimStateComponent->CachedMoveInput = FVector2D(Right, Forward);
	if (AnimStateComponent)
	{
		AnimStateComponent->SetMoveInput(Right, Forward);
	}
	RefreshSprintFromInput();

	const FVector2D ClampedMoveInput = FVector2D(Right, Forward).GetClampedToMaxSize(1.f);
	if (HasAuthority())
	{
		AuthoritativeMoveInput = ClampedMoveInput;
		bHasAuthoritativeMoveInput = true;
	}
	if (IsLocallyControlled() && !HasAuthority())
	{
		const bool bShouldSendMoveInput =
			!bHasSentMoveInputToServer ||
			!LastSentMoveInputToServer.Equals(ClampedMoveInput, 0.01f);
		if (bShouldSendMoveInput)
		{
			LastSentMoveInputToServer = ClampedMoveInput;
			bHasSentMoveInputToServer = true;
			Server_SetMoveInput(ClampedMoveInput);
		}
	}

	// if (IsBasePlayerMotionMatchingCaptureEnabled() && IsLocallyControlled())
	// {
	// 	const FString DebugLine = FString::Printf(
	// 		TEXT("[MMCAP_INPUT] Pawn=%s Net=%d Role=%d Source=LocalMove Raw=(R=%.2f,F=%.2f) Clamped=(R=%.2f,F=%.2f) Sprint=%d"),
	// 		*GetName(),
	// 		static_cast<int32>(GetNetMode()),
	// 		static_cast<int32>(GetLocalRole()),
	// 		Right,
	// 		Forward,
	// 		ClampedMoveInput.X,
	// 		ClampedMoveInput.Y,
	// 		(AnimStateComponent && AnimStateComponent->bIsSprinting) ? 1 : 0);
	// 	// UE_LOG(LogTemp, Display, TEXT("%s"), *DebugLine);
	// 	AppendBasePlayerMotionMatchingCaptureLine(DebugLine);
	// }

	if (GetController() != nullptr)
	{
		const FRotator Rotation = GetController()->GetControlRotation();
		const FRotator YawRotation(0, Rotation.Yaw, 0);

		const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
		const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

		AddMovementInput(ForwardDirection, Forward);
		AddMovementInput(RightDirection, Right);
	}
}

void ABasePlayer::StopMoveInput()
{
	if(AnimStateComponent) AnimStateComponent->ClearMoveInput();
	if (AnimStateComponent)
	{
		AnimStateComponent->ClearMoveInput();
	}
	RefreshSprintFromInput();

	if (IsLocallyControlled() && !HasAuthority())
	{
		LastSentMoveInputToServer = FVector2D::ZeroVector;
		bHasSentMoveInputToServer = true;
		Server_SetMoveInput(FVector2D::ZeroVector);
	}
	else if (HasAuthority())
	{
		AuthoritativeMoveInput = FVector2D::ZeroVector;
		bHasAuthoritativeMoveInput = true;
	}

	// if (IsBasePlayerMotionMatchingCaptureEnabled() && IsLocallyControlled())
	// {
	// 	const FString DebugLine = FString::Printf(
	// 		TEXT("[MMCAP_INPUT] Pawn=%s Net=%d Role=%d Source=LocalStop Raw=(R=0.00,F=0.00) Clamped=(R=0.00,F=0.00) Sprint=%d"),
	// 		*GetName(),
	// 		static_cast<int32>(GetNetMode()),
	// 		static_cast<int32>(GetLocalRole()),
	// 		(AnimStateComponent && AnimStateComponent->bIsSprinting) ? 1 : 0);
	// 	// UE_LOG(LogTemp, Display, TEXT("%s"), *DebugLine);
	// 	AppendBasePlayerMotionMatchingCaptureLine(DebugLine);
	// }
}

void ABasePlayer::DoLook(float Yaw, float Pitch)
{
	if (GetController() != nullptr)
	{
		float Multiplier = 1.0f;
		if (CachedAbilitySystemComponent.IsValid() && CachedAbilitySystemComponent->HasMatchingGameplayTag(State_Sniping))
		{
			Multiplier = SnipingMouseSensitivity;
		}

		AddControllerYawInput(Yaw * Multiplier);
		AddControllerPitchInput(Pitch * Multiplier);
	}
}

void ABasePlayer::DoJumpStart()
{
	if (!CanJump())
	{
		return;
	}

	Jump();

	if (AnimStateComponent)
	{
		AnimStateComponent->HandleJumpStarted();
	}

	if (HasAuthority())
	{
		LocomotionStateSnapshot.EventSequence = NextLocomotionAnimEventSequence();
		LocomotionStateSnapshot.LastLocomotionEvent = EReplicatedLocomotionEvent::Jump;
	}
	else
	{
		Server_NotifyJumpStarted();
	}

}

void ABasePlayer::DoJumpEnd()
{
	StopJumping();
}

void ABasePlayer::StartSprint()
{
	bSprintInputHeld = true;
	RefreshSprintFromInput();
}

void ABasePlayer::StopSprint()
{
	bSprintInputHeld = false;
	if (!AnimStateComponent || !AnimStateComponent->bIsSprinting)
	{
		return;
	}

	
	if (AnimStateComponent)
	{
		AnimStateComponent->SetSprinting(false);
	}

	if (!HasAuthority())
	{
		Server_SetSprinting(false);
	}
}

bool ABasePlayer::CanSprintFromInput() const
{
	const bool bBlockedByAbilityState =
		CachedAbilitySystemComponent.IsValid() &&
		CachedAbilitySystemComponent->HasMatchingGameplayTag(State_Attacking);

	return AnimStateComponent && !bBlockedByAbilityState
		? AnimStateComponent->CachedMoveInput.Y > 0.15f
		: false;
}

void ABasePlayer::RefreshSprintFromInput()
{
	if (!IsLocallyControlled())
	{
		return;
	}

	const bool bShouldSprint =
		bSprintInputHeld &&
		CanSprintFromInput() &&
		!bIsAttacking &&
		!bIsDodging &&
		!bIsHitReacting;

	if ((AnimStateComponent ? AnimStateComponent->bIsSprinting : false) == bShouldSprint)
	{
		return;
	}

	
	if (AnimStateComponent)
	{
		AnimStateComponent->SetSprinting(bShouldSprint);
	}

	if (!HasAuthority())
	{
		Server_SetSprinting(bShouldSprint);
	}
}

void ABasePlayer::Server_SetSprinting_Implementation(bool bNewSprinting)
{
	const bool bServerAllowsSprinting = bNewSprinting && CanSprintFromServerState();
	
	if (AnimStateComponent)
	{
		AnimStateComponent->SetSprinting(bServerAllowsSprinting);
		UpdateLocomotionStateSnapshot();
	}
}

void ABasePlayer::Server_SetMoveInput_Implementation(FVector2D NewMoveInput)
{
	const FVector2D ClampedMoveInput = NewMoveInput.GetClampedToMaxSize(1.f);
	
	AuthoritativeMoveInput = ClampedMoveInput;
	bHasAuthoritativeMoveInput = true;

	if (AnimStateComponent)
	{
		if (ClampedMoveInput.SizeSquared() > FMath::Square(AnimStateComponent->MoveInputDeadZone))
		{
			AnimStateComponent->SetMoveInput(ClampedMoveInput.X, ClampedMoveInput.Y);
		}
		else
		{
			AnimStateComponent->ClearMoveInput();
		}

		UpdateLocomotionStateSnapshot();
	}

	// if (IsBasePlayerMotionMatchingCaptureEnabled())
	// {
	// 	const FString DebugLine = FString::Printf(
	// 		TEXT("[MMCAP_SERVER_INPUT] Pawn=%s Net=%d Role=%d Received=(R=%.2f,F=%.2f) Clamped=(R=%.2f,F=%.2f) HasInput=%d"),
	// 		*GetName(),
	// 		static_cast<int32>(GetNetMode()),
	// 		static_cast<int32>(GetLocalRole()),
	// 		NewMoveInput.X,
	// 		NewMoveInput.Y,
	// 		ClampedMoveInput.X,
	// 		ClampedMoveInput.Y,
	// 		ClampedMoveInput.SizeSquared() > FMath::Square(AnimStateComponent ? AnimStateComponent->MoveInputDeadZone : 0.1f) ? 1 : 0);
	// 	UE_LOG(LogTemp, Display, TEXT("%s"), *DebugLine);
	// 	AppendBasePlayerMotionMatchingCaptureLine(DebugLine);
	// }
}

void ABasePlayer::OnRep_LocomotionStateSnapshot(const FReplicatedLocomotionState& OldSnapshot)
{
	if (AnimStateComponent)
	{
		// if (const IConsoleVariable* DebugCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("p.MMDebugging"));
		// 	DebugCVar && DebugCVar->GetInt() > 0)
		// {
		// 	UE_LOG(LogTemp, Display, TEXT("[MMCAP_EVENT] OnRep_LocomotionStateSnapshot Pawn=%s Seq=%d (Old=%d) Event=%d HasInput=%d MoveInput=(R=%.2f,F=%.2f) LandDir=(R=%.2f,F=%.2f) LandGround=%.1f Fall=%.1f Sprint=%d"),
		// 		*GetName(),
		// 		LocomotionStateSnapshot.EventSequence, OldSnapshot.EventSequence,
		// 		(int32)LocomotionStateSnapshot.LastLocomotionEvent,
		// 		LocomotionStateSnapshot.bHasMoveInput ? 1 : 0,
		// 		LocomotionStateSnapshot.MoveInput.X,
		// 		LocomotionStateSnapshot.MoveInput.Y,
		// 		LocomotionStateSnapshot.LandMoveDirection.X,
		// 		LocomotionStateSnapshot.LandMoveDirection.Y,
		// 		LocomotionStateSnapshot.LandStartGroundSpeed,
		// 		LocomotionStateSnapshot.LastFallSpeed,
		// 		LocomotionStateSnapshot.bIsSprinting ? 1 : 0);
		// }


		// 데이터 기반 이벤트 처리 (새로운 EventSequence가 오면 LastLocomotionEvent를 기반으로 애니메이션 컴포넌트에 통보)
		AnimStateComponent->ApplyAuthoritativeSnapshot(LocomotionStateSnapshot);

		if (LocomotionStateSnapshot.EventSequence != OldSnapshot.EventSequence)
		{
			switch (LocomotionStateSnapshot.LastLocomotionEvent)
			{
			case EReplicatedLocomotionEvent::Jump:
				AnimStateComponent->HandleRemoteJumpStarted(LocomotionStateSnapshot.EventSequence);
				break;
			case EReplicatedLocomotionEvent::FallOff:
				AnimStateComponent->HandleRemoteFallOffStarted(LocomotionStateSnapshot.EventSequence);
				break;
			case EReplicatedLocomotionEvent::Landed:
				AnimStateComponent->HandleRemoteLanded(LocomotionStateSnapshot.LastFallSpeed, LocomotionStateSnapshot.EventSequence);
				break;
			default:
				break;
			}
		}

	}
}

void ABasePlayer::Server_NotifyJumpStarted_Implementation()
{
	if (AnimStateComponent)
	{
		AnimStateComponent->HandleJumpStarted();
	}

	// 이벤트 통보 (Multicast 대신 Snapshot에 변수 세팅)
	LocomotionStateSnapshot.EventSequence = NextLocomotionAnimEventSequence();
	LocomotionStateSnapshot.LastLocomotionEvent = EReplicatedLocomotionEvent::Jump;
}

void ABasePlayer::BroadcastFallOffStartedForRemoteClients()
{
	if (HasAuthority())
	{
		LocomotionStateSnapshot.EventSequence = NextLocomotionAnimEventSequence();
		LocomotionStateSnapshot.LastLocomotionEvent = EReplicatedLocomotionEvent::FallOff;
	}
}

void ABasePlayer::OnRep_ItemSlots()
{
	OnItemSlotsChanged.Broadcast();
}

void ABasePlayer::OnRep_QuickSlots()
{
	OnQuickSlotsChanged.Broadcast();
}

void ABasePlayer::ApplyCombatRotationMode(bool bEnableCombatRotation)
{
	bUseControllerRotationYaw = false;

	if (UCharacterMovementComponent* MovementComponent = GetCharacterMovement())
	{
		MovementComponent->bOrientRotationToMovement = false;
		MovementComponent->bUseControllerDesiredRotation = true;
	}
}


void ABasePlayer::RequestCombatModeToggle()
{
	SetCombatMode(!bIsCombatMode);
}

void ABasePlayer::SetCombatMode(bool bNewCombatMode)
{
	if (bNewCombatMode == bIsCombatMode && !bIsPlayingCombatIntro)
	{
		return;
	}

	UAnimMontage* ResolvedCombatIntroMontage = CombatIntroMontage;
	float ResolvedCombatIntroPlayRate = CombatIntroMontagePlayRate;
	if (EquipmentComponent)
	{
		if (UAnimMontage* EquipmentCombatIntroMontage = EquipmentComponent->GetEquippedCombatIntroMontage())
		{
			ResolvedCombatIntroMontage = EquipmentCombatIntroMontage;
			ResolvedCombatIntroPlayRate = EquipmentComponent->GetEquippedCombatIntroPlayRate();
		}
	}

	if (!bNewCombatMode)
	{
		bIsCombatMode = false;
		bPendingCombatModeFromIntro = false;
		bIsPlayingCombatIntro = false;
		ApplyCombatRotationMode(false);

		if (UAnimInstance* AnimInstance = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr)
		{
			if (ActiveCombatIntroMontage)
			{
				AnimInstance->Montage_Stop(0.1f, ActiveCombatIntroMontage);
			}
		}
		ActiveCombatIntroMontage = nullptr;
		return;
	}

	if (ResolvedCombatIntroMontage)
	{
		bPendingCombatModeFromIntro = true;
		bIsPlayingCombatIntro = true;
		ApplyCombatRotationMode(true);
		ActiveCombatIntroMontage = ResolvedCombatIntroMontage;

		if (UAnimInstance* AnimInstance = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr)
		{
			const float MontageLength = AnimInstance->Montage_Play(ResolvedCombatIntroMontage, ResolvedCombatIntroPlayRate);
			if (MontageLength > 0.f)
			{
				FOnMontageEnded MontageEndedDelegate;
				MontageEndedDelegate.BindUObject(this, &ABasePlayer::OnCombatIntroMontageEnded);
				AnimInstance->Montage_SetEndDelegate(MontageEndedDelegate, ResolvedCombatIntroMontage);
				return;
			}
		}
	}

	bIsCombatMode = true;
	ApplyCombatRotationMode(true);
	bPendingCombatModeFromIntro = false;
	bIsPlayingCombatIntro = false;
	ActiveCombatIntroMontage = nullptr;
}

void ABasePlayer::EnterCombatModeFromEquipment()
{
	bIsCombatMode = true;
	bPendingCombatModeFromIntro = false;
	bIsPlayingCombatIntro = false;
	ActiveCombatIntroMontage = nullptr;
	ApplyCombatRotationMode(true);
}

void ABasePlayer::InterruptCombatIntroForHit()
{
	if (!bIsPlayingCombatIntro || !bInterruptCombatIntroOnHit)
	{
		return;
	}

	if (UAnimInstance* AnimInstance = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr)
	{
		if (ActiveCombatIntroMontage)
		{
			AnimInstance->Montage_Stop(0.1f, ActiveCombatIntroMontage);
		}
	}

	bIsPlayingCombatIntro = false;
	bPendingCombatModeFromIntro = false;
	ActiveCombatIntroMontage = nullptr;
	if (!bIsCombatMode)
	{
		ApplyCombatRotationMode(false);
	}
}

void ABasePlayer::OnCombatIntroMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	if (Montage != ActiveCombatIntroMontage)
	{
		return;
	}

	bIsPlayingCombatIntro = false;
	ActiveCombatIntroMontage = nullptr;

	if (!bInterrupted && bPendingCombatModeFromIntro)
	{
		bIsCombatMode = true;
		ApplyCombatRotationMode(true);
	}
	else if (!bIsCombatMode)
	{
		ApplyCombatRotationMode(false);
	}

	bPendingCombatModeFromIntro = false;
}

void ABasePlayer::Landed(const FHitResult& Hit)
{
	const float ImpactFallSpeed = FMath::Max((AnimStateComponent ? AnimStateComponent->LastFallSpeed : 0.f), FMath::Abs(GetVelocity().Z));

	// if (const IConsoleVariable* DebugCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("p.MMDebugging"));
	// 	DebugCVar && DebugCVar->GetInt() > 0)
	// {
	// 	UE_LOG(LogTemp, Display,
	// 		TEXT("[MMCAP_EVENT] Character::Landed Impact=%.1f CachedLastFall=%.1f Velocity=(%.1f,%.1f,%.1f) HitActor=%s HitNormal=(%.2f,%.2f,%.2f)"),
	// 		ImpactFallSpeed,
	// 		(AnimStateComponent ? AnimStateComponent->LastFallSpeed : 0.f),
	// 		GetVelocity().X,
	// 		GetVelocity().Y,
	// 		GetVelocity().Z,
	// 		*GetNameSafe(Hit.GetActor()),
	// 		Hit.ImpactNormal.X,
	// 		Hit.ImpactNormal.Y,
	// 		Hit.ImpactNormal.Z);
	// }

	Super::Landed(Hit);

	if (AnimStateComponent)
	{
		AnimStateComponent->HandleLanded(Hit, ImpactFallSpeed);
	}

	if (HasAuthority())
	{
		LocomotionStateSnapshot.EventSequence = NextLocomotionAnimEventSequence();
		LocomotionStateSnapshot.LastLocomotionEvent = EReplicatedLocomotionEvent::Landed;
		// FallSpeed는 UpdateLocomotionStateSnapshot 시점에 LastFallSpeed로 복제됨
	}
}

int32 ABasePlayer::NextLocomotionAnimEventSequence()
{
	++LocomotionAnimEventSequence;
	if (LocomotionAnimEventSequence <= 0)
	{
		LocomotionAnimEventSequence = 1;
	}

	return LocomotionAnimEventSequence;
}

bool ABasePlayer::CanSprintFromServerState() const
{
	const bool bBlockedByAbilityState =
		CachedAbilitySystemComponent.IsValid() &&
		CachedAbilitySystemComponent->HasMatchingGameplayTag(State_Attacking);

	return !bBlockedByAbilityState && !bIsAttacking && !bIsDodging && !bIsHitReacting;
}
