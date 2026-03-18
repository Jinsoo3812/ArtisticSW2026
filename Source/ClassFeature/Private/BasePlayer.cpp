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
	: SlotTag(InTag), Item(InItem) {}

bool FItemSlot::operator==(const FGameplayTag& OtherTag) const { return SlotTag == OtherTag; }

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
	if (SlotInputConfig)
	{
		for (const FSlotInputAction& Action : SlotInputConfig->SlotInputActions)
		{
			if (Action.SlotTag.IsValid() && Action.SlotTag.MatchesTag(Key_Item))
			{
				ItemSlots.Add(FItemSlot(Action.SlotTag));
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

		// Interact (아이템 줍기)
		if (InteractAction)
		{
			EnhancedInputComponent->BindAction(InteractAction, ETriggerEvent::Started, this, &ABasePlayer::Interact);
		}

		// ItemSlot 입력 바인딩
		if (SlotInputConfig)
		{
			for (const FSlotInputAction& Action : SlotInputConfig->SlotInputActions)
			{
				if (Action.InputAction && Action.SlotTag.IsValid())
				{
					EnhancedInputComponent->BindAction(Action.InputAction, ETriggerEvent::Started, this, &ABasePlayer::EquipItemFromSlot, Action.SlotTag);
				}
			}
		}

		//  마우스 왼클릭 바인딩
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
	if (!FollowCamera) return;

	FVector StartLoc = GetActorLocation();

	// 캐릭터가 바라보는 정면 방향으로 짧은 거리만큼 쏨
	FVector EndLoc = StartLoc + (FollowCamera->GetForwardVector() * InteractTraceDistance);

	FHitResult HitResult;
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this); // 내 자신은 스캔에서 제외

	// 구체 형태의 Sweep 발사
	bool bHit = GetWorld()->SweepSingleByChannel(
		HitResult,
		StartLoc,
		EndLoc,
		FQuat::Identity,
		ECC_Interactable, // 프로젝트 세팅의 ECC_Interactable 실제 채널로 변경
		FCollisionShape::MakeSphere(InteractTraceRadius), // 
		QueryParams
	);

#if ENABLE_DRAW_DEBUG
	FColor DrawColor = bHit ? FColor::Green : FColor::Red;

	// 시작점과 끝점을 잇는 원통(캡슐) 형태로 직관적으로 한 번에 그리기
	FVector TraceCenter = StartLoc + (EndLoc - StartLoc) * 0.5f;
	float TraceHalfHeight = (EndLoc - StartLoc).Size() * 0.5f;
	FQuat TraceRotation = FRotationMatrix::MakeFromZ(EndLoc - StartLoc).ToQuat();

	DrawDebugCapsule(GetWorld(), TraceCenter, TraceHalfHeight, InteractTraceRadius, TraceRotation, DrawColor, false, 2.0f);
#endif

	if (bHit && HitResult.GetComponent())
	{
		// 타겟이 인터페이스를 구현했는지 확인
		if (IInteractable* InteractableComp = Cast<IInteractable>(HitResult.GetComponent()))
		{
			// 상호 작용 호출 (상호 작용 객체의 내부 처리)
			InteractableComp->Interact(this);

			// Player의 처리는 Tag를 기반으로 분기
			FGameplayTag TargetTag = InteractableComp->GetInteractionTag();

			// 분기해야 할 Tag의 종류가 많아지면 Switch 문 등으로 리팩토링 고려
			if (TargetTag.MatchesTag(Interaction_PickUp))
			{
				if (ABaseItem* HitItem = Cast<ABaseItem>(HitResult.GetActor()))
				{
					// 손에 붙이는 것 까지는 이미 수행된 상태이므로, 슬롯에 넣는 시도만 수행
					TryPutItemInSlot(HitItem);
				}
			}
		}
	}
	

	/* ------------------ LAGACY ----------------------*/
	/*
	// 모든 Item 및 기타 상호작용 가능 객체가 UInterface를 구현하였을 때 삭제 예정
	TArray<AActor*> OverlappingActors;
	GetOverlappingActors(OverlappingActors, ABaseItem::StaticClass());

	for (AActor* Actor : OverlappingActors)
	{
		if (ABaseItem* Item = Cast<ABaseItem>(Actor))
		{
			// 현재 손에 든 Item은 무시
			if (IsValid(EquippedItem) && Item == EquippedItem.Get())
			{
				continue;
			}

			// ItemSlots 배열에서 빈 슬롯(nullptr)의 인덱스를 찾음
			int32 EmptySlotIndex = ItemSlots.IndexOfByPredicate([](const FItemSlot& Slot)
				{
					return !IsValid(Slot.Item);
				});

			if (EmptySlotIndex != INDEX_NONE)
			{
				// 일단 주워서 손에 붙여
				Item->PickUpItem(this);
				ItemSlots[EmptySlotIndex].Item = Item; // 빈 슬롯에 저장

				if (IsValid(EquippedItem))
				{
					// 이미 손에 든 게 있는 경우
					// PickUpItem 때문에 방금 주운 아이템이 손에 붙어버렸으므로 렌더링을 꺼서 안 보이게 처리
					Item->SetActorHiddenInGame(true);
				}
				else
				{
					// 손에 든 게 없는 경우
					EquipItemFromSlot(ItemSlots[EmptySlotIndex].SlotTag);
					Item->SetActorHiddenInGame(false); // 보이게 처리
				}
				// 한 번의 상호작용으로 하나의 아이템만 줍도록 루프 종료
				break;
			}
			else
			{
				// 인벤토리가 꽉 찼을 경우
				UE_LOG(LogTemp, Warning, TEXT("ABasePlayer::Interact : ItemSlot is Full."));
				break;
			}
		}
	}
	*/
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
			Item->SetActorHiddenInGame(true);
		}
		else
		{
			// 손이 비어있으면 새로 주운 아이템 바로 장착
			EquipItemFromSlot(ItemSlots[EmptySlotIndex].SlotTag);
			Item->SetActorHiddenInGame(false);
		}
		return true; // 성공적으로 슬롯에 넣음
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("ABasePlayer::TryPutItemInSlot : ItemSlot is Full."));
		return false; // 인벤토리 꽉 참
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
	UE_LOG(LogTemp, Log, TEXT("ABasePlayer::GrantAbilityToSlot : Granted ability %s to SlotTag %s"), *AbilityClass->GetName(), *SlotTag.ToString());
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

		RemoveItemFromSlot(ItemSlots[EquippedIndex].SlotTag);
	}
}

void ABasePlayer::EquipItemFromSlot(FGameplayTag SlotTag)
{
	// [서버]
	if (!HasAuthority()) return;

	int32 SlotIndex = ItemSlots.IndexOfByKey(SlotTag);

	if (ItemSlots.IsValidIndex(SlotIndex))
	{
		ABaseItem* TargetItem = ItemSlots[SlotIndex].Item;

		// 선택한 슬롯이 비어있는 경우 (맨손 처리)
		if (!IsValid(TargetItem))
		{
			if (IsValid(EquippedItem))
			{
				RemoveAbilityFromSlot(Ability_Item_Equipped);
				EquippedItem->SetActorHiddenInGame(true);
				EquippedItem = nullptr;
			}
			return;
		}

		// 선택한 슬롯에 아이템이 있는 경우
		if (EquippedItem == TargetItem)
		{
			return; // 이미 장착된 아이템이면 아무 작업도 하지 않음
		}

		// 기존 장착 아이템 해제 (파괴하지 않고 숨김 처리)
		if (IsValid(EquippedItem))
		{
			RemoveAbilityFromSlot(Ability_Item_Equipped);
			EquippedItem->SetActorHiddenInGame(true);
		}

		// 장착 아이템 갱신
		EquippedItem = TargetItem;

		EquippedItem->PickUpItem(this); // 아이템을 손에 붙이는 처리 (구조 수정해야 함)

		// 새 아이템 보여주기 및 어빌리티 부여
		if (IsValid(EquippedItem))
		{
			// 숨겨뒀던 아이템의 렌더링을 켬
			EquippedItem->SetActorHiddenInGame(false);

			bool bShouldGrantAbility = false;
			const TArray<FGameplayTag>& RequiredTags = EquippedItem->GetCanUseAbilityList();

			// 필요 직업 태그가 없다면 그냥 부여
			if (RequiredTags.IsEmpty())
			{
				bShouldGrantAbility = true;
			}
			else if (AbilitySystemComponent)
			{
				// 요구 태그를 하나라도 가지고 있으면 부여
				for (const FGameplayTag& Tag : RequiredTags)
				{
					if (AbilitySystemComponent->HasMatchingGameplayTag(Tag))
					{
						bShouldGrantAbility = true;
						break;
					}
				}
			}

			// 조건 만족 시에만 GA 부여
			if (bShouldGrantAbility)
			{
				GrantAbilityToSlot(Ability_Item_Equipped, EquippedItem->GetGrantedAbilityClass());
			}
		}
	}
}

void ABasePlayer::RemoveItemFromSlot(FGameplayTag SlotTag)
{
	// [서버]
	if (!HasAuthority()) return;

	int32 SlotIndex = ItemSlots.IndexOfByKey(SlotTag);

	if (ItemSlots.IsValidIndex(SlotIndex) && IsValid(ItemSlots[SlotIndex].Item))
	{
		ItemSlots[SlotIndex].Item = nullptr;
		RemoveAbilityFromSlot(SlotTag);
	}
}

void ABasePlayer::OnMouseLeftPressed()
{
	if (!AbilitySystemComponent) return;

	// 현재 장착된 Item이 있다면 그 GA 실행
	OnAbilityInputPressed(Ability_Item_Equipped);

	// ASC에 GameplayEvent로서 전달
	FGameplayEventData EventData;
	EventData.Instigator = this;
	EventData.Target = nullptr;

	// 활성화된 모든 어빌리티 중, 이 태그를 기다리는(WaitGameplayEvent) GA에게 신호.
	AbilitySystemComponent->HandleGameplayEvent(Input_MouseLeftClick, &EventData);
}

void ABasePlayer::OnMouseLeftReleased()
{
	if (!AbilitySystemComponent) return;

	OnAbilityInputReleased(Ability_Item_Equipped);
}

void ABasePlayer::OnMouseRightPressed() {
	// ASC에 GameplayEvent로서 전달
	FGameplayEventData EventData;
	EventData.Instigator = this;
	EventData.Target = nullptr;

	// 활성화된 모든 어빌리티 중, 이 태그를 기다리는(WaitGameplayEvent) GA에게 신호.
	AbilitySystemComponent->HandleGameplayEvent(Input_MouseRightClick, &EventData);
}

void ABasePlayer::OnRep_EquippedItem(ABaseItem* OldItem)
{
	// 서버가 EquippedItem을 변경하면 모든 클라이언트에서 실행됨
	if (EquippedItem)
	{
		// 클라이언트에서도 아이템을 보이게 하고 손에 부착
		EquippedItem->SetActorHiddenInGame(false);
		EquippedItem->AttachToComponent(GetMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, TEXT("GripPoint"));
	}
	
	// 이전 아이템은 숨김 처리 (필요 시)
	if (OldItem && OldItem != EquippedItem)
	{
		OldItem->SetActorHiddenInGame(true);
	}
}