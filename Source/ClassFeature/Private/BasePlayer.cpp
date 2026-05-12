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
#include "AbilitySystemBlueprintLibrary.h"
#include "Components/WidgetComponent.h"
#include "InteractableComponent.h"
#include "InteractUserWidget.h"
#include "Inventory/InventoryComponent.h"
#include "ItemSubSystem.h"

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

	// 인벤토리 컴포넌트 부착
	InventoryComponent = CreateDefaultSubobject<UInventoryComponent>(TEXT("InventoryComponent"));
}

void ABasePlayer::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// 배열과 장착 아이템 포인터를 클라이언트로 복제
	DOREPLIFETIME(ABasePlayer, ItemSlots);
	DOREPLIFETIME(ABasePlayer, EquippedItem);
}

void ABasePlayer::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	// 모든 KeyTag와 InputID를 매핑
	InitializeInputIDMap();
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

	OnItemSlotsChanged.Broadcast();
}

void ABasePlayer::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	UpdateAnimationMovementState();

	// 조준 상태 확인 (GA에서 State_Aiming 태그를 부여했다고 가정)
	bool bIsAimingState = false;
	bool bIsAttackingState = false; // 공격( 현재 구현된 무기 기준으로는 투척) 중인지 확인

	if (CachedAbilitySystemComponent.Get())
	{
		bIsAimingState = CachedAbilitySystemComponent->HasMatchingGameplayTag(State_Aiming);
		bIsAttackingState = CachedAbilitySystemComponent->HasMatchingGameplayTag(State_Attacking);
	}

	// 캐릭터 회전은 조준 중(Aiming)이거나 던지는 중(Attacking)일 때 모두 고정
	bool bLockRotation = bIsAimingState || bIsAttackingState;
	bUseControllerRotationYaw = bLockRotation;
	GetCharacterMovement()->bOrientRotationToMovement = !bLockRotation;

	// 카메라 줌인은 오직 조준 중(Aiming)일 때만 작동! (던질 땐 줌아웃됨)
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
		CachedAbilitySystemComponent = PS->GetAbilitySystemComponent();

		// Interact GA에 의해 발생한 Gameplay Event를 처리할 콜백 함수 등록
		// 현재는 Event 별로 따로 바인딩하지만 더 좋은 방법이 있을까?
		if(CachedAbilitySystemComponent.IsValid()) {
			CachedAbilitySystemComponent->GenericGameplayEventCallbacks.FindOrAdd(Interaction_PickUp).AddUObject(this, &ABasePlayer::HandlePickUpEvent);

			// Map에 등록된 기본 어빌리티 순회 및 슬롯에 부여
			for (const auto& AbilityPair : DefaultAbilityMap)
			{
				if (AbilityPair.Value)
				{
					GrantAbilityToSlot(AbilityPair.Key, AbilityPair.Value);
				}
			}
		}

		/*// 부모 클래스에 구현된 어빌리티 부여 함수 호출 (서버에서만)
		GrantAbilities(StartingAbilities);*/
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
		CachedAbilitySystemComponent = PS->GetAbilitySystemComponent();
		if (CachedAbilitySystemComponent.Get()) {
			//CachedAbilitySystemComponent->GenericGameplayEventCallbacks.FindOrAdd(Interaction_PickUp).AddUObject(this, &ABasePlayer::HandlePickUpEvent);
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

	// 해당 슬롯에 이미 부여된 어빌리티가 있다면 교체
	RemoveAbilityFromSlot(KeyTag);

	// 통합 맵에서 이 태그에 할당된 ID를 가져옴
	int32 AssignedID = GetInputIDFromTag(KeyTag);

	// GA Spec 생성 시 해당 ID 주입
	FGameplayAbilitySpec Spec(AbilityClass, 1, AssignedID, this);
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
	if (!CachedAbilitySystemComponent.Get() || !InputTag.IsValid()) return;

	int32 InputID = GetInputIDFromTag(InputTag);
	if (InputID != INDEX_NONE)
	{
		UE_LOG(LogTemp, Log, TEXT("ABasePlayer::OnAbilityInputPressed : Input pressed with KeyTag: %s, InputID: %d"), *InputTag.ToString(), InputID);
		CachedAbilitySystemComponent->AbilityLocalInputPressed(InputID);
	}
}

void ABasePlayer::OnAbilityInputReleased(FGameplayTag InputTag)
{
	if (!CachedAbilitySystemComponent.Get() || !InputTag.IsValid()) return;

	int32 InputID = GetInputIDFromTag(InputTag);
	if (InputID != INDEX_NONE)
	{
		CachedAbilitySystemComponent->AbilityLocalInputReleased(InputID);
	}
}

void ABasePlayer::OnMouseInputPressed(FGameplayTag InputTag)
{
	if (!CachedAbilitySystemComponent.Get() || !InputTag.IsValid()) return;

	// 공통 GAS 입력 해제 처리
	OnAbilityInputPressed(InputTag);

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
			// 아이템 태그가 material 로 시작하면 인벤토리로
			if (ItemToPickUp->ItemTag.MatchesTag(Item_Material))
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

		OnItemSlotsChanged.Broadcast();
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
		EquippedItem->AttachToComponent(GetMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, EquippedItem->MyDefinition->AttachmentSocketName);

		// 새 아이템 GA 부여 로직
		bool bShouldGrantAbility = false;
		const TArray<FGameplayTag>& RequiredTags = EquippedItem->GetCanUseAbilityList();

		if (RequiredTags.IsEmpty())
		{
			bShouldGrantAbility = true;
		}
		else if (CachedAbilitySystemComponent.Get())
		{
			for (const FGameplayTag& Tag : RequiredTags)
			{
				if (CachedAbilitySystemComponent->HasMatchingGameplayTag(Tag))
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

	OnItemSlotsChanged.Broadcast();
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
	// 새 아이템이 유효하다면 손 소켓에 부착
	if (IsValid(EquippedItem) && EquippedItem->MyDefinition)
	{
		if (UStaticMeshComponent* MeshComp = Cast<UStaticMeshComponent>(EquippedItem->GetRootComponent()))
		{
			MeshComp->SetSimulatePhysics(false);
			MeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
		EquippedItem->AttachToComponent(GetMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, EquippedItem->MyDefinition->AttachmentSocketName);
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
	FColor DrawColor = bHit ? FColor::Green : FColor::Red;
	FVector TraceCenter = StartLoc + (EndLoc - StartLoc) * 0.5f;
	float TraceHalfHeight = (EndLoc - StartLoc).Size() * 0.5f;
	FQuat TraceRotation = FRotationMatrix::MakeFromZ(EndLoc - StartLoc).ToQuat();

	// 타이머 주기에 맞춰 그려지도록 LifeTime을 짧게 설정 (예: 0.1초)
	DrawDebugCapsule(GetWorld(), TraceCenter, TraceHalfHeight, InteractTraceRadius, TraceRotation, DrawColor, false, 0.1f);
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
			if (UWidgetComponent* WidgetComp = HitActor->FindComponentByClass<UWidgetComponent>())
			{
				CurrentHoveredWidgets.AddUnique(WidgetComp);
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
	GetWorldTimerManager().ClearTimer(JumpStartTimerHandle);
	GetWorldTimerManager().ClearTimer(LandingTimerHandle);
	StopFallOffStart();

	bIsLanding = false;
	bIsInAir = true;
	bWasInAir = true;
	bSuppressFallOffStart = true;
	bIsJumping = true;

	GetWorldTimerManager().SetTimer(
		JumpStartTimerHandle,
		this,
		&ABasePlayer::FinishJumpStart,
		JumpStartDuration,
		false
	);

	Jump();
}

void ABasePlayer::DoJumpEnd()
{
	StopJumping();
}

void ABasePlayer::OnRep_ItemSlots()
{
	OnItemSlotsChanged.Broadcast();
}

bool ABasePlayer::IsInAirForAnimation() const
{
	const UCharacterMovementComponent* MovementComponent = GetCharacterMovement();
	return MovementComponent && MovementComponent->MovementMode == MOVE_Falling;
}

void ABasePlayer::UpdateAnimationMovementState()
{
	const FVector Velocity = GetVelocity();
	VerticalSpeed = Velocity.Z;

	FVector HorizontalVelocity = Velocity;
	HorizontalVelocity.Z = 0.f;
	GroundSpeed = HorizontalVelocity.Size();

	const bool bNowInAir = IsInAirForAnimation();

	if (!bWasInAir
		&& bNowInAir
		&& !bIsJumping
		&& !bIsLanding
		&& !bSuppressFallOffStart)
	{
		StartFallOffStart();
	}

	if (bNowInAir)
	{
		bIsInAir = true;
	}
	else if (!bIsJumping)
	{
		bIsInAir = false;
		bSuppressFallOffStart = false;
	}

	bWasInAir = bNowInAir;
}

void ABasePlayer::FinishJumpStart()
{
	bIsJumping = false;
}

void ABasePlayer::StartFallOffStart()
{
	bIsInAir = true;
	bIsFallOffStart = true;

	GetWorldTimerManager().ClearTimer(FallOffStartTimerHandle);
	GetWorldTimerManager().SetTimer(
		FallOffStartTimerHandle,
		this,
		&ABasePlayer::FinishFallOffStart,
		FallOffStartDuration,
		false
	);
}

void ABasePlayer::FinishFallOffStart()
{
	StopFallOffStart();
}

void ABasePlayer::StopFallOffStart()
{
	GetWorldTimerManager().ClearTimer(FallOffStartTimerHandle);
	bIsFallOffStart = false;
}

void ABasePlayer::FinishLanding()
{
	bIsLanding = false;
}

void ABasePlayer::Landed(const FHitResult& Hit)
{
	// 항상 Super를 먼저 호출해주어야 캐릭터 무브먼트의 기본 착지 로직이 꼬이지 않습니다.
	Super::Landed(Hit);

	GetWorldTimerManager().ClearTimer(JumpStartTimerHandle);
	StopFallOffStart();

	bIsJumping = false;
	bIsInAir = false;
	bWasInAir = false;
	bSuppressFallOffStart = false;
	bIsLanding = true;

	GetWorldTimerManager().ClearTimer(LandingTimerHandle);
	GetWorldTimerManager().SetTimer(
		LandingTimerHandle,
		this,
		&ABasePlayer::FinishLanding,
		LandingDuration,
		false
	);

	// 착지하는 순간의 Z축 하강 속도를 가져옵니다.
	// (CharacterMovementComponent에서 물리 연산 직전의 Z 속도를 보존하고 있습니다)
	float FallSpeed = GetCharacterMovement()->Velocity.Z;

	// 디버그용: 실제 게임에서 떨어졌을 때 속도가 얼마나 나오는지 확인해보세요.
	// UE_LOG(LogTemp, Log, TEXT("Landed! Z Velocity: %f"), FallSpeed);

	// 하강 속도가 일정 수치 이상일 때만 '진짜 착지'로 인정 (Z속도는 음수이므로 < 사용)
	// -300.0f 는 예시이며, 점프 높이에 따라 -500.0f 등으로 조절하세요.
	if (FallSpeed < -300.0f)
	{
		// 1. 애니메이션 재생을 위해 블루프린트(Character BP)로 신호를 보냅니다.
		K2_OnRealLanded();

		// 2. (보너스) 현재 GAS를 완벽하게 세팅해 두셨으니, 
		// 착지 순간에 딜레이나 무적, 공격 불가 등의 상태(GE)를 부여하고 싶다면
		// 여기서 바로 GameplayEvent를 발생시킬 수도 있습니다!
		/*
		if (CachedAbilitySystemComponent.Get())
		{
			FGameplayEventData Payload;
			Payload.Instigator = this;
			// Tag_Event_Movement_Landed 등 태그를 만들어 매핑
			CachedAbilitySystemComponent->HandleGameplayEvent(Tag_Event_Movement_Landed, &Payload);
		}
		*/
	}
}
