// Fill out your copyright notice in the Description page of Project Settings.


#include "BasePlayer.h"
#include "BasePlayerState.h"
#include "AbilitySystemComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"
#include "BaseItem.h"
#include "CrafterComponent.h"
#include "BaseGameplayTags.h"
#include "Net/UnrealNetwork.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "ItemData.h"
#include "Interactable.h"
#include "CollisionChannels.h"
#include "BaseGameplayTags.h"

/* --- FItemSlot ---*/

FItemSlot::FItemSlot(const FGameplayTag& InTag, ABaseItem* InItem)
	: KeyTag(InTag), Item(InItem) {}

bool FItemSlot::operator==(const FGameplayTag& OtherTag) const { return KeyTag == OtherTag; }

bool FItemSlot::operator==(const ABaseItem* OtherItem) const { return Item.Get() == OtherItem; }



/* --- BasePlayer ---*/

ABasePlayer::ABasePlayer()
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
}

void ABasePlayer::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// 배열과 장착 아이템 포인터를 클라이언트로 복제
	DOREPLIFETIME(ABasePlayer, ItemSlots);
	DOREPLIFETIME(ABasePlayer, EquippedItem);
}

void ABasePlayer::BeginPlay()
{
	Super::BeginPlay();

	// ItemSlot 배열 초기화: TMap 등록 없이 구조체 배열에 순서대로 Add
	if (ItemInputConfig)
	{
		for (const FKeyInputAction& Action : ItemInputConfig->KeyInputActions)
		{
			if (Action.KeyTag.IsValid() && Action.KeyTag.MatchesTag(Key_Item))
			{
				ItemSlots.Add(FItemSlot(Action.KeyTag));
			}
		}
	}
}

void ABasePlayer::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// 조준 상태 확인 (GA에서 State_Aiming 태그를 부여했다고 가정)
	bool bIsAimingState = false;
	if (AbilitySystemComponent)
	{
		bIsAimingState = AbilitySystemComponent->HasMatchingGameplayTag(State_Aiming);
	}

	// 캐릭터 회전 설정
	bUseControllerRotationYaw = bIsAimingState;
	GetCharacterMovement()->bOrientRotationToMovement = !bIsAimingState;

	// 카메라 보간 로직
	if (CameraBoom)
	{
		float TargetArmLength = bIsAimingState ? AimingTargetArmLength : DefaultTargetArmLength;
		FVector TargetSocketOffset = bIsAimingState ? AimingSocketOffset : DefaultSocketOffset;

		CameraBoom->TargetArmLength = FMath::FInterpTo(CameraBoom->TargetArmLength, TargetArmLength, DeltaTime, CameraInterpSpeed);
		CameraBoom->SocketOffset = FMath::VInterpTo(CameraBoom->SocketOffset, TargetSocketOffset, DeltaTime, CameraInterpSpeed);
	}
}

void ABasePlayer::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	// 서버 측 ASC 초기화 (InitAbilityActorInfo)
	ABasePlayerState* PS = GetPlayerState<ABasePlayerState>();
	if (PS)
	{
		// Owner는 PlayerState, Avatar는 이 Character 객체로 설정
		PS->GetAbilitySystemComponent()->InitAbilityActorInfo(PS, this);

		// PlayerState로 부터 ASC 포인터 가져와서 캐싱
		AbilitySystemComponent = PS->GetAbilitySystemComponent();
		if(AbilitySystemComponent) {
			AbilitySystemComponent->GenericGameplayEventCallbacks.FindOrAdd(Interaction_PickUp).AddUObject(this, &ABasePlayer::HandlePickUpEvent);

			// Map에 등록된 기본 어빌리티 순회 및 슬롯에 부여
			for (const auto& AbilityPair : DefaultAbilityMap)
			{
				if (AbilityPair.Value)
				{
					GrantAbilityToSlot(AbilityPair.Key, AbilityPair.Value);
				}
			}
		}

		// 부모 클래스에 구현된 어빌리티 부여 함수 호출 (서버에서만)
		GrantAbilities(StartingAbilities);
	}

	// ASC 초기화 완료 알림 방송
	OnAbilitySystemInitialized.Broadcast();
}

void ABasePlayer::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();

	// 클라이언트 측 ASC 초기화 (PlayerState가 클라로 복제되었음을 보장하는 타이밍)
	ABasePlayerState* PS = GetPlayerState<ABasePlayerState>();
	if (PS)
	{
		// 클라이언트에서도 Owner와 Avatar를 연결해줌
		PS->GetAbilitySystemComponent()->InitAbilityActorInfo(PS, this);

		// 클라이언트 측 포인터 갱신
		AbilitySystemComponent = PS->GetAbilitySystemComponent();
		if (AbilitySystemComponent) {
			//AbilitySystemComponent->GenericGameplayEventCallbacks.FindOrAdd(Interaction_PickUp).AddUObject(this, &ABasePlayer::HandlePickUpEvent);
		}
	}
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

		// Default 입력 바인딩
		if (DefaultInputConfig)
		{
			for (const FKeyInputAction& Action : DefaultInputConfig->KeyInputActions)
			{
				if (Action.InputAction && Action.KeyTag.IsValid())
				{
					EnhancedInputComponent->BindAction(Action.InputAction, ETriggerEvent::Started, this, &ABasePlayer::OnAbilityInputPressed, Action.KeyTag);
					EnhancedInputComponent->BindAction(Action.InputAction, ETriggerEvent::Completed, this, &ABasePlayer::OnAbilityInputReleased, Action.KeyTag);
				}
			}
		}

		// ItemSlot 입력 바인딩
		if (ItemInputConfig)
		{
			for (const FKeyInputAction& Action : ItemInputConfig->KeyInputActions)
			{
				if (Action.InputAction && Action.KeyTag.IsValid())
				{
					EnhancedInputComponent->BindAction(Action.InputAction, ETriggerEvent::Started, this, &ABasePlayer::EquipItemFromSlot, Action.KeyTag);
				}
			}
		}

		//  마우스 바인딩
		if (MouseLeftAction)
		{
			EnhancedInputComponent->BindAction(MouseLeftAction, ETriggerEvent::Started, this, &ABasePlayer::OnMouseLeftPressed);
			EnhancedInputComponent->BindAction(MouseLeftAction, ETriggerEvent::Completed, this, &ABasePlayer::OnMouseLeftReleased);
		}

		if (MouseRightAction)
		{
			EnhancedInputComponent->BindAction(MouseRightAction, ETriggerEvent::Started, this, &ABasePlayer::OnMouseRightPressed);
		}
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

		Item->PickUpItem(this);
		
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
	if (!HasAuthority() || !AbilitySystemComponent || !AbilityClass || !KeyTag.IsValid())
	{
		return;
	}

	// 해당 슬롯에 이미 부여된 어빌리티가 있다면 교체
	RemoveAbilityFromSlot(KeyTag);

	// GA Spec 생성
	FGameplayAbilitySpec Spec(AbilityClass, 1, INDEX_NONE, this);

	// GA Spec에 KeyTag를 동적 추가
	Spec.GetDynamicSpecSourceTags().AddTag(KeyTag);

	// ASC에 GA 부여
	AbilitySystemComponent->GiveAbility(Spec);
	UE_LOG(LogTemp, Log, TEXT("ABasePlayer::GrantAbilityToSlot : Granted ability %s to KeyTag %s"), *AbilityClass->GetName(), *KeyTag.ToString());
}

void ABasePlayer::RemoveAbilityFromSlot(FGameplayTag KeyTag)
{
	if (!HasAuthority() || !AbilitySystemComponent || !KeyTag.IsValid())
	{
		return;
	}

	// Spec 배열 순회중 제거 작업 등이 일어나면 안되므로 Scope Lock 사용
	FScopedAbilityListLock AbilityLock(*AbilitySystemComponent);

	TArray<FGameplayAbilitySpecHandle> HandlesToRemove;

	// ASC의 모든 GA Spec 순회
	for (const FGameplayAbilitySpec& Spec : AbilitySystemComponent->GetActivatableAbilities())
	{
		// 부여할 때 심어둔 동적 KeyTag를 포함하는지 확인
		if (Spec.GetDynamicSpecSourceTags().HasTag(KeyTag))
		{
			HandlesToRemove.Add(Spec.Handle);
		}
	}

	// 수집된 Handle 일괄 제거
	for (const FGameplayAbilitySpecHandle& Handle : HandlesToRemove)
	{
		AbilitySystemComponent->ClearAbility(Handle);
	}
	
	// AbilityLock 소멸
}

void ABasePlayer::OnAbilityInputPressed(FGameplayTag InputTag)
{
	UE_LOG(LogTemp, Log, TEXT("ABasePlayer::OnAbilityInputPressed : called with InputTag: %s"), *InputTag.ToString());
	if (!AbilitySystemComponent || !InputTag.IsValid())
	{
		return;
	}

	// Spec 배열 순회중 제거 작업 등이 일어나면 안되므로 Scope Lock 사용
	FScopedAbilityListLock AbilityLock(*AbilitySystemComponent);

	for (FGameplayAbilitySpec& Spec : AbilitySystemComponent->GetActivatableAbilities())
	{
		// 입력 처리 시에는 Tag가 정확히 일치하는 GA만 실행
		if (Spec.GetDynamicSpecSourceTags().HasTagExact(InputTag))
		{
			// 로컬 입력 상태를 눌림(Pressed)으로 수동 변경
			Spec.InputPressed = true;

			if (Spec.IsActive())
			{
				// 이미 실행 중인 GA라면 InputPressed 이벤트 통지
				AbilitySystemComponent->AbilitySpecInputPressed(Spec);
			}
			else
			{
				// 실행 중이 아니라면 GA 활성화 시도
				AbilitySystemComponent->TryActivateAbility(Spec.Handle);
			}
		}
	}

	// AbilityLock 소멸
}

void ABasePlayer::OnAbilityInputReleased(FGameplayTag InputTag)
{
	if (!AbilitySystemComponent || !InputTag.IsValid())
	{
		return;
	}

	// Spec 배열 순회중 제거 작업 등이 일어나면 안되므로 Scope Lock 사용
	FScopedAbilityListLock AbilityLock(*AbilitySystemComponent);

	for (FGameplayAbilitySpec& Spec : AbilitySystemComponent->GetActivatableAbilities())
	{
		if (Spec.GetDynamicSpecSourceTags().HasTagExact(InputTag))
		{
			// 로컬 입력 상태를 뗌(Released)으로 수동 변경
			Spec.InputPressed = false;

			if (Spec.IsActive())
			{
				// 실행 중인 GA에 키를 뗐다는 이벤트 통지
				AbilitySystemComponent->AbilitySpecInputReleased(Spec);	
			}
		}
	}

	// AbilityLock 소멸
}

void ABasePlayer::HandlePickUpEvent(const FGameplayEventData* Payload)
{
	if (Payload && Payload->Target)
	{
		if (ABaseItem* ItemToPickUp = const_cast<ABaseItem*>(Cast<ABaseItem>(Payload->Target)))
		{
			TryPutItemInSlot(ItemToPickUp);
		}
	}
}

void ABasePlayer::UseEquippedItem(bool bDestroy)
{
	// 서버 권한 및 장착 아이템 유효성 검사
	if (!HasAuthority() || EquippedItem == nullptr)
	{
		return;
	}

	// 장착된 아이템이 ItemSlots 배열의 몇 번째 인덱스에 있는지 탐색
	int32 EquippedIndex = ItemSlots.IndexOfByKey(EquippedItem.Get());

	if (EquippedIndex != INDEX_NONE)
	{
		UE_LOG(LogTemp, Log, TEXT("ABasePlayer::UseEquippedItem : Item used! Slot index: %d"), EquippedIndex);

		if (bDestroy) {
			EquippedItem->Destroy(); // 아이템 액터 제거
		}
		
		// 손에 들고 있는 장착 상태 해제
		EquippedItem = nullptr;

		RemoveItemFromSlot(ItemSlots[EquippedIndex].KeyTag);
	}
}

void ABasePlayer::EquipItemFromSlot(FGameplayTag KeyTag)
{
	// 현재는 장착형 아이템이기 때문에 ItemSlot에 대한 키 입력이 들어올 경우 손에 장착하는 로직만 있지만
	// 추후에 즉발형 아이템의 경우 Item 자체의 Tag로 분기하여 손에 들지않고 즉시 사용되도록 추가해야 함.
	// OnAbilityInputPressed으로 바로 넘기면 될 듯? 그게 즉발형을 위한 바인딩 함수니까.

	// [클라이언트] 권한이 없다면 서버로 RPC 요청
	if (!HasAuthority())
	{
		Server_EquipItemFromSlot(KeyTag);
		// UI 등 로컬 예측을 하고 싶다면 여기서
		return;
	}

	// [서버]
	int32 SlotIndex = ItemSlots.IndexOfByKey(KeyTag);

	if (ItemSlots.IsValidIndex(SlotIndex))
	{
		ABaseItem* SlotItem = ItemSlots[SlotIndex].Item;

		// 이미 손에 들고 있는 Item이라면 아무 작업도 하지 않음
		if (EquippedItem == SlotItem) return;


		// 기존 아이템은 안보이게 넣음
		if (IsValid(EquippedItem))
		{
			RemoveAbilityFromSlot(Key_Default_MouseLeftClick);
			EquippedItem->SetItemState(EItemState::InItemSlot);
		}

		// 새 아이템이 없다면 맨손으로 만들고 종료
		if (!IsValid(SlotItem))
		{
			EquippedItem = nullptr;
			return;
		}

		// 장착 아이템 갱신
		EquippedItem = SlotItem;
		EquippedItem->SetItemState(EItemState::Equipped);

		// 새 아이템 GA 부여 로직
		bool bShouldGrantAbility = false;
		const TArray<FGameplayTag>& RequiredTags = EquippedItem->GetCanUseAbilityList();

		if (RequiredTags.IsEmpty())
		{
			bShouldGrantAbility = true;
		}
		else if (AbilitySystemComponent)
		{
			for (const FGameplayTag& Tag : RequiredTags)
			{
				if (AbilitySystemComponent->HasMatchingGameplayTag(Tag))
				{
					bShouldGrantAbility = true;
					break;
				}
			}
		}

		if (bShouldGrantAbility)
		{
			GrantAbilityToSlot(Key_Default_MouseLeftClick, EquippedItem->GetGrantedAbilityClass());
		}
	}
}

void ABasePlayer::Server_EquipItemFromSlot_Implementation(FGameplayTag KeyTag)
{
	// 서버가 다시 본래의 함수를 호출하여 권한(HasAuthority)을 통과시키고 실제 로직을 실행
	EquipItemFromSlot(KeyTag);
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
	}
}

void ABasePlayer::OnMouseLeftPressed()
{
	if (!AbilitySystemComponent) return;

	// 현재 장착된 Item이 있다면 그 GA 실행
	OnAbilityInputPressed(Key_Default_MouseLeftClick);

	// ASC에 GameplayEvent로서 전달
	FGameplayEventData EventData;
	EventData.Instigator = this;
	EventData.Target = nullptr;

	// 활성화된 모든 어빌리티 중, 이 태그를 기다리는(WaitGameplayEvent) GA에게 신호.
	AbilitySystemComponent->HandleGameplayEvent(Event_Input_MouseLeftClick, &EventData);
}

void ABasePlayer::OnMouseLeftReleased()
{
	if (!AbilitySystemComponent) return;

	OnAbilityInputReleased(Key_Default_MouseLeftClick);
}

void ABasePlayer::OnMouseRightPressed() {
	// ASC에 GameplayEvent로서 전달
	FGameplayEventData EventData;
	EventData.Instigator = this;
	EventData.Target = nullptr;

	// 활성화된 모든 어빌리티 중, 이 태그를 기다리는(WaitGameplayEvent) GA에게 신호.
	AbilitySystemComponent->HandleGameplayEvent(Event_Input_MouseRightClick, &EventData);
}

void ABasePlayer::OnRep_EquippedItem()
{
	// 새 아이템이 유효하다면 손 소켓에 부착
	if (IsValid(EquippedItem) && EquippedItem->MyDefinition)
	{
		EquippedItem->AttachToComponent(GetMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, EquippedItem->MyDefinition->AttachmentSocketName);
	}
}

void ABasePlayer::Move(const FInputActionValue& Value)
{
	FVector2D MovementVector = Value.Get<FVector2D>();
	DoMove(MovementVector.X, MovementVector.Y);
}

void ABasePlayer::Look(const FInputActionValue& Value)
{
	FVector2D LookAxisVector = Value.Get<FVector2D>();
	DoLook(LookAxisVector.X, LookAxisVector.Y);
}

void ABasePlayer::DoMove(float Right, float Forward)
{
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

void ABasePlayer::DoLook(float Yaw, float Pitch)
{
	if (GetController() != nullptr)
	{
		AddControllerYawInput(Yaw);
		AddControllerPitchInput(Pitch);
	}
}

void ABasePlayer::DoJumpStart()
{
	Jump();
}

void ABasePlayer::DoJumpEnd()
{
	StopJumping();
}