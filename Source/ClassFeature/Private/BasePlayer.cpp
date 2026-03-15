// Fill out your copyright notice in the Description page of Project Settings.


#include "BasePlayer.h"
#include "BasePlayerState.h"
#include "AbilitySystemComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"
#include "Item/BaseItem.h"
#include "CrafterComponent.h"
#include "BaseGameplayTags.h"
#include "Net/UnrealNetwork.h"

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

	// 캐시 테이블 초기화
	if (SlotInputConfig)
	{
		// 배열 크기 미리 할당
		ItemSlots.SetNum(SlotInputConfig->ItemSlotCount);
		IndexToItemSlotTagArray.SetNum(SlotInputConfig->ItemSlotCount);

		int32 CurrentItemSlotIndex = 0;
		for (const FSlotInputAction& Action : SlotInputConfig->SlotInputActions)
		{
			// Item 관련 슬롯 태그만 추출 (Slot.Item)
			if (Action.SlotTag.IsValid() && Action.SlotTag.MatchesTag(Slot_Item))
			{
				if (IndexToItemSlotTagArray.IsValidIndex(CurrentItemSlotIndex))
				{
					// 양방향 캐싱 등록
					ItemSlotTagToIndexMap.Add(Action.SlotTag, CurrentItemSlotIndex);
					IndexToItemSlotTagArray[CurrentItemSlotIndex] = Action.SlotTag;
					CurrentItemSlotIndex++;
				}
			}
		}
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

		// Interact (아이템 줍기)
		if (InteractAction)
		{
			EnhancedInputComponent->BindAction(InteractAction, ETriggerEvent::Started, this, &ABasePlayer::Interact);
		}

		// ItemSlot 입력 바인딩
		if (SlotInputConfig)
		{
			// 모든 IA를 하나의 콜백 함수에 바인딩 (Tag로 Event 구분)
			for (const FSlotInputAction& Action : SlotInputConfig->SlotInputActions)
			{
				if (Action.InputAction && Action.SlotTag.IsValid())
				{
					// 키를 눌렀을 때 (Started) -> Tag를 인자로 담아 OnAbilityInputPressed 호출
					EnhancedInputComponent->BindAction(Action.InputAction, ETriggerEvent::Started, this, &ABasePlayer::OnAbilityInputPressed, Action.SlotTag);

					// 키를 뗐을 때 (Completed) -> 차징 스킬 등을 위해
					EnhancedInputComponent->BindAction(Action.InputAction, ETriggerEvent::Completed, this, &ABasePlayer::OnAbilityInputReleased, Action.SlotTag);
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
	Jump();
}

void ABasePlayer::DoJumpEnd()
{
	StopJumping();
}

void ABasePlayer::Interact()
{
	if (EquippedItem != nullptr) return;

	TArray<AActor*> OverlappingActors;
	GetOverlappingActors(OverlappingActors, ABaseItem::StaticClass());

	for (AActor* Actor : OverlappingActors)
	{
		if (ABaseItem* Item = Cast<ABaseItem>(Actor))
		{
			Item->PickUpItem(this);
			EquippedItem = Item;

			ItemSlots[0] = Item; // 임시로 첫 번째 슬롯에 장착

			GrantAbilityToSlot(Slot_Item_1, Item->GrantedAbilityClass); // 임시로 첫 번째 슬롯에 장착

			break;
		}
	}
}

void ABasePlayer::GrantAbilityToSlot(FGameplayTag SlotTag, TSubclassOf<UGameplayAbility> AbilityClass)
{
	// 서버에서만 실행되며, 유효성 검사 수행
	if (!HasAuthority() || !AbilitySystemComponent || !AbilityClass || !SlotTag.IsValid())
	{
		return;
	}

	// 해당 슬롯에 이미 부여된 어빌리티가 있다면 교체
	RemoveAbilityFromSlot(SlotTag);

	// GA Spec 생성
	FGameplayAbilitySpec Spec(AbilityClass, 1, INDEX_NONE, this);

	// GA Spec에 SlotTag를 동적 추가
	Spec.GetDynamicSpecSourceTags().AddTag(SlotTag);

	// ASC에 GA 부여
	AbilitySystemComponent->GiveAbility(Spec);
}

void ABasePlayer::RemoveAbilityFromSlot(FGameplayTag SlotTag)
{
	if (!HasAuthority() || !AbilitySystemComponent || !SlotTag.IsValid())
	{
		return;
	}

	// Spec 배열 순회중 제거 작업 등이 일어나면 안되므로 Scope Lock 사용
	FScopedAbilityListLock AbilityLock(*AbilitySystemComponent);

	TArray<FGameplayAbilitySpecHandle> HandlesToRemove;

	// ASC의 모든 GA Spec 순회
	for (const FGameplayAbilitySpec& Spec : AbilitySystemComponent->GetActivatableAbilities())
	{
		// 부여할 때 심어둔 동적 SlotTag를 포함하는지 확인
		if (Spec.GetDynamicSpecSourceTags().HasTag(SlotTag))
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
	UE_LOG(LogTemp, Log, TEXT("OnAbilityInputPressed called with InputTag: %s"), *InputTag.ToString());
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
		else UE_LOG(LogTemp, Warning, TEXT("AbilitySpec does not have the required InputTag %s"), *InputTag.ToString());
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

void ABasePlayer::ThrowEquippedItem()
{
	// 서버 권한 및 장착 아이템 유효성 검사
	if (!HasAuthority() || EquippedItem == nullptr)
	{
		return;
	}

	// 장착된 아이템이 ItemSlots 배열의 몇 번째 인덱스에 있는지 탐색
	int32 EquippedIndex = ItemSlots.Find(EquippedItem);

	if (EquippedIndex != INDEX_NONE)
	{
		UE_LOG(LogTemp, Log, TEXT("[ThrowEquippedItem] 아이템 투척 완료! 기존 슬롯 인덱스: %d"), EquippedIndex);

		EquippedItem->Destroy(); // 던지기 구현 전에 일단 임시로 제거

		// 손에 들고 있는 장착 상태 해제
		EquippedItem = nullptr;

		// ItemSlot Index를 이용해 SlotTag를 찾아내기
		const FGameplayTag* FoundTag = ItemSlotTagToIndexMap.FindKey(EquippedIndex);
		if (FoundTag)
		{
			// ItemSlot 정리 및 GA 회수
			RemoveItemFromSlot(*FoundTag);
		}
	}
}

void ABasePlayer::RemoveItemFromSlot(FGameplayTag SlotTag)
{
	// [서버]
	if (!HasAuthority()) return;

	int32 SlotIndex = GetItemSlotIndexByTag(SlotTag);

	if (ItemSlots.IsValidIndex(SlotIndex) && ItemSlots[SlotIndex] != nullptr)
	{
		// ItemSlot 배열에서 제거
		ItemSlots[SlotIndex] = nullptr;

		// GA 회수
		RemoveAbilityFromSlot(SlotTag);
	}
}

int32 ABasePlayer::GetItemSlotIndexByTag(const FGameplayTag& SlotTag) const
{
	if (const int32* FoundIndex = ItemSlotTagToIndexMap.Find(SlotTag))
	{
		return *FoundIndex;
	}
	return INDEX_NONE;
}