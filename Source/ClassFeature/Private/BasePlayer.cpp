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
#include "AbilitySystemBlueprintLibrary.h"

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

	// 1. 캐릭터가 컨트롤러(마우스)의 좌우 회전(Yaw)을 똑같이 따라가도록 설정
	bUseControllerRotationYaw = true;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;

	// 2. 캐릭터가 이동하는 방향(WASD)으로 몸을 홱 돌려버리는 기능 끄기
	GetCharacterMovement()->bOrientRotationToMovement = false;
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

	// 모든 KeyTag와 InputID를 매핑
	InitializeInputIDMap();

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

	// 캐릭터 회전 설정 모션 매칭 작업을 위해 off

	/*bUseControllerRotationYaw = bIsAimingState;
	GetCharacterMovement()->bOrientRotationToMovement = !bIsAimingState;*/

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
	}
}

void ABasePlayer::InitializeInputIDMap()
{
	InputTagToIDMap.Empty();
	int32 NextID = 0;

	// 모든 Config를 하나의 배열로 통합
	TArray<UInputTagConfig*> Configs;
	if (DefaultInputConfig) Configs.Add(DefaultInputConfig);
	if (ItemInputConfig) Configs.Add(ItemInputConfig);

	if (UCrafterComponent* CrafterComp = FindComponentByClass<UCrafterComponent>())
	{
		if (UInputTagConfig* CrafterConfig = CrafterComp->CrafterInputConfig)
		{
			Configs.Add(CrafterConfig);
		}
	}

	// 각 Config를 순회하며 태그에 고유 ID 할당
	for (UInputTagConfig* Config : Configs)
	{
		for (const FKeyInputAction& Action : Config->KeyInputActions)
		{
			if (Action.KeyTag.IsValid() && !InputTagToIDMap.Contains(Action.KeyTag))
			{
				UE_LOG(LogTemp, Log, TEXT("ABasePlayer::InitializeInputIDMap : Assigning InputID %d to KeyTag: %s"), NextID, *Action.KeyTag.ToString());
				InputTagToIDMap.Add(Action.KeyTag, NextID++);
			}
		}
	}


}

int32 ABasePlayer::GetInputIDFromTag(const FGameplayTag& Tag) const
{
	const int32* FoundID = InputTagToIDMap.Find(Tag);
	return FoundID ? *FoundID : INDEX_NONE;
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

	// 통합 맵에서 이 태그에 할당된 ID를 가져옴
	int32 AssignedID = GetInputIDFromTag(KeyTag);

	// GA Spec 생성 시 해당 ID 주입
	FGameplayAbilitySpec Spec(AbilityClass, 1, AssignedID, this);
	AbilitySystemComponent->GiveAbility(Spec);
}

void ABasePlayer::RemoveAbilityFromSlot(FGameplayTag KeyTag)
{
	if (!HasAuthority() || !AbilitySystemComponent || !KeyTag.IsValid())
	{
		return;
	}

	int32 TargetInputID = GetInputIDFromTag(KeyTag);
	if (TargetInputID == INDEX_NONE) return;

	// GAS 내부의 부여된 어빌리티 목록을 순회하며 매핑된 InputID를 가진 어빌리티 수집 및 제거
	TArray<FGameplayAbilitySpecHandle> HandlesToRemove;
	for (const FGameplayAbilitySpec& Spec : AbilitySystemComponent->GetActivatableAbilities())
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
		AbilitySystemComponent->ClearAbility(Handle);
	}
}

void ABasePlayer::OnAbilityInputPressed(FGameplayTag InputTag)
{
	if (!AbilitySystemComponent || !InputTag.IsValid()) return;

	int32 InputID = GetInputIDFromTag(InputTag);
	if (InputID != INDEX_NONE)
	{
		UE_LOG(LogTemp, Log, TEXT("ABasePlayer::OnAbilityInputPressed : Input pressed with KeyTag: %s, InputID: %d"), *InputTag.ToString(), InputID);
		AbilitySystemComponent->AbilityLocalInputPressed(InputID);
	}
}

void ABasePlayer::OnAbilityInputReleased(FGameplayTag InputTag)
{
	if (!AbilitySystemComponent || !InputTag.IsValid()) return;

	int32 InputID = GetInputIDFromTag(InputTag);
	if (InputID != INDEX_NONE)
	{
		AbilitySystemComponent->AbilityLocalInputReleased(InputID);
	}
}

void ABasePlayer::OnMouseInputPressed(FGameplayTag InputTag)
{
	if (!AbilitySystemComponent || !InputTag.IsValid()) return;

	// 공통 GAS 입력 해제 처리
	OnAbilityInputPressed(InputTag);

	// ASC에 GameplayEvent로서 전달
	FGameplayEventData EventData;
	EventData.Instigator = this;
	EventData.Target = nullptr;

	if (!HasAuthority())
	{
		ServerRPC_SendGameplayEvent(InputTag, EventData);
	}
}

void ABasePlayer::OnMouseInputReleased(FGameplayTag InputTag)
{
	// 공통 GAS 입력 해제 처리
	OnAbilityInputReleased(InputTag);

	// 여기는 Tag에 Released를 붙여야 하는데 귀찮아서 아직 안함
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
		// 현재는 마우스로 사용하는 Item밖에 없으므로 이렇게 하지만 추후에는 Tag로 분기할 것
		RemoveItemFromSlot(ItemSlots[EquippedIndex].KeyTag);
		RemoveAbilityFromSlot(Key_Default_Mouse_LeftClick);

		if (bDestroy) {
			EquippedItem->Destroy(); // 아이템 액터 제거
		}
		
		// 손에 들고 있는 장착 상태 해제
		EquippedItem = nullptr;
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
			RemoveAbilityFromSlot(Key_Default_Mouse_LeftClick);
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
			GrantAbilityToSlot(Key_Default_Mouse_LeftClick, EquippedItem->GetGrantedAbilityClass());
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

void ABasePlayer::ServerRPC_SendGameplayEvent_Implementation(FGameplayTag EventTag, FGameplayEventData Payload)
{
	// 서버의 ASC에서 이벤트를 발생시켜 WaitGameplayEvent 태스크를 깨웁니다.
	UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(this, EventTag, Payload);
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