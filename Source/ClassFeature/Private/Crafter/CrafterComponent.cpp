// Fill out your copyright notice in the Description page of Project Settings.


#include "CrafterComponent.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputTagConfig.h"
#include "BasePlayer.h"
#include "BaseGameplayTags.h"
#include "InteractableComponent.h"
#include "BaseGameplayTags.h"
#include "StarForceWidget.h"
#include "WorkTable.h"
#include "InventoryComponent.h"
#include "StarForceWidget.h"
#include "ItemData.h"
#include "BaseItem.h"
#include "ItemSubsystem.h"

UCrafterComponent::UCrafterComponent()
{
	// Component는 굳이 tick이 필요 없으니까
	PrimaryComponentTick.bCanEverTick = false;

	// Component 복제 활성화
	SetIsReplicatedByDefault(true);
}


void UCrafterComponent::BeginPlay()
{
	Super::BeginPlay();

	if (ABasePlayer* Player = Cast<ABasePlayer>(GetOwner()))
	{
		// [서버] Component 초기화 로직 수행
		if (Player->HasAuthority())
		{
			// 이미 ASC가 초기화되어 있다면 대기하지 않고 즉시 어빌리티 부여 (StandAlone 대응)
			if (Player->GetAbilitySystemComponent())
			{
				SetupCrafterSystem();
				UE_LOG(LogTemp, Log, TEXT("CrafterComponent: ASC already initialized, setting up immediately for %s"), *Player->GetName());
			}
			else
			{
				// 아직 ASC가 셋업되지 않았다면 델리게이트 바인딩
				Player->OnAbilitySystemInitialized.AddUObject(this, &UCrafterComponent::SetupCrafterSystem);
				UE_LOG(LogTemp, Log, TEXT("CrafterComponent: ASC not initialized yet, binding delegate for %s"), *Player->GetName());
			}
		}
	}
}

void UCrafterComponent::SetupCrafterSystem()
{
	GrantCrafterAbilities();

	ABasePlayer* Player = Cast<ABasePlayer>(GetOwner());
	if (Player && Player->GetAbilitySystemComponent())
	{
		// 서버 측 이벤트 바인딩
		Player->GetAbilitySystemComponent()->GenericGameplayEventCallbacks.FindOrAdd(Interaction_Craft).AddUObject(this, &UCrafterComponent::HandleCraftEvent);
	}
}

void UCrafterComponent::AddCrafterMappingContext()
{
	ABasePlayer* Player = Cast<ABasePlayer>(GetOwner());

	if (!Player) return;

	// OwnerPlayer의 Controller를 가져와서 PlayerController로 캐스팅
	if (APlayerController* PC = Cast<APlayerController>(Player->GetController()))
	{
		// 로컬 플레이어의 Enhanced Input Subsystem 획득
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
		{
			if (CrafterIMC)
			{
				// 설정된 우선순위(Priority)로 IMC 추가
				Subsystem->AddMappingContext(CrafterIMC, CrafterIMCPriority);
			}
		}
	}
}

void UCrafterComponent::RemoveCrafterMappingContext()
{
	ABasePlayer* Player = Cast<ABasePlayer>(GetOwner());

	if (!Player) return;

	if (APlayerController* PC = Cast<APlayerController>(Player->GetController()))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
		{
			if (CrafterIMC)
			{
				// 할당된 IMC 제거
				Subsystem->RemoveMappingContext(CrafterIMC);
			}
		}
	}
}

void UCrafterComponent::BindCrafterInput(UEnhancedInputComponent* EnhancedInputComponent)
{
	ABasePlayer* Player = Cast<ABasePlayer>(GetOwner());

	if (!EnhancedInputComponent || !CrafterInputConfig || !Player) return;

	// IMC 우선순위로 인해 동일 키입력에 대해 우선 적용
	for (const FKeyInputAction& Action : CrafterInputConfig->KeyInputActions)
	{
		if (Action.InputAction && Action.KeyTag.IsValid())
		{
			// Crafter GA에 대한 바인딩
			EnhancedInputComponent->BindAction(Action.InputAction, ETriggerEvent::Started, Player, &ABasePlayer::OnAbilityInputPressed, Action.KeyTag);
			EnhancedInputComponent->BindAction(Action.InputAction, ETriggerEvent::Completed, Player, &ABasePlayer::OnAbilityInputReleased, Action.KeyTag);

			// ESC 키에 대한 별도 바인딩 (Crafting 종료)
			if (Action.KeyTag == Key_Default_ESC) {
				EnhancedInputComponent->BindAction(Action.InputAction, ETriggerEvent::Started, this, &UCrafterComponent::EndCrafting);
			}

			// Space 키에 대한 별도 바인딩 (스타포스 플레이)
			if (Action.KeyTag == Key_Default_Space) {
				EnhancedInputComponent->BindAction(Action.InputAction, ETriggerEvent::Started, this, &UCrafterComponent::SpaceBarAction);
			}
		}
	}
}

void UCrafterComponent::GrantCrafterAbilities()
{
	ABasePlayer* Player = Cast<ABasePlayer>(GetOwner());

	if (!Player || !Player->HasAuthority()) return;

	// ASC에 Crafter 역할 태그 부여
	if (UAbilitySystemComponent* ASC = Player->GetAbilitySystemComponent())
	{
		ASC->AddLooseGameplayTag(Class_Crafter);
	}

	// 설정용 Map을 순회하면서 Player의 Grant 함수를 호출
	for (const auto& SlotGAPair : CrafterAbilities)
	{
		Player->GrantAbilityToSlot(SlotGAPair.Key, SlotGAPair.Value);
	}
}

void UCrafterComponent::RemoveCrafterAbilities()
{
	ABasePlayer* Player = Cast<ABasePlayer>(GetOwner());

	if (!Player || !Player->HasAuthority()) return;

	// ASC에 Crafter 역할 태그 해제
	if (UAbilitySystemComponent* ASC = Player->GetAbilitySystemComponent())
	{
		ASC->RemoveLooseGameplayTag(Class_Crafter);
	}

	// 설정용 Map을 순회하면서 해당 태그의 어빌리티만 지워달라고 요청
	for (const auto& SlotGAPair : CrafterAbilities)
	{
		Player->RemoveAbilityFromSlot(SlotGAPair.Key);
	}
}

void UCrafterComponent::HandleCraftEvent(const FGameplayEventData* Payload)
{
	if (!Payload || !Payload->Target) return;

	AActor* TargetActor = const_cast<AActor*>(Cast<AActor>(Payload->Target));
	ABasePlayer* Player = Cast<ABasePlayer>(GetOwner());

	if (!TargetActor || !Player) return;

	// [서버]에서 이벤트를 받았다면 클라이언트에게 UI를 띄우라고 RPC 전송
	if (Player->HasAuthority())
	{
		ClientRPC_OpenCraftingUI(TargetActor);
	}
	// [클라이언트]에서 직접 이벤트를 받았다면 (로컬 예측 등) 바로 UI 띄움
	else if (Player->IsLocallyControlled())
	{
		ShowCraftingUI(TargetActor);
	}
}

void UCrafterComponent::ClientRPC_OpenCraftingUI_Implementation(AActor* TargetActor)
{
	// 서버의 명령을 받아 클라이언트에서 실행됨
	ShowCraftingUI(TargetActor);
}

void UCrafterComponent::ShowCraftingUI(AActor* TargetActor)
{
	ABasePlayer* Player = Cast<ABasePlayer>(GetOwner());
	if (!Player) return;

	CurrentInteractingTable = Cast<AWorkTable>(TargetActor);
	UInteractableComponent* InteractComp = CurrentInteractingTable->FindComponentByClass<UInteractableComponent>();
	if (!InteractComp || !InteractComp->InteractPopupUIClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("UCrafterComponent::HandleCraftEvent : Can't find InteractableComponent or UI Class is not assigned."));
		return;
	}

	if (APlayerController* PC = Cast<APlayerController>(Player->GetController())) {
		// 기존에 띄워진 UI가 있다면 제거하여 중복 생성 방지
		if (ActiveCraftingUI)
		{
			ActiveCraftingUI->RemoveFromParent();
			ActiveCraftingUI = nullptr;
		}

		// UI 위젯 생성
		ActiveCraftingUI = CreateWidget<UUserWidget>(PC, InteractComp->InteractPopupUIClass);
		if (ActiveCraftingUI)
		{
			// 생성한 UI가 UStarForceWidget이라면 이벤트 구독(Bind)
			if (UStarForceWidget* StarForceUI = Cast<UStarForceWidget>(ActiveCraftingUI))
			{
				// 이전에 바인딩된 게 있다면 지우고 새로 바인딩
				StarForceUI->OnStarforceAttempted.AddDynamic(this, &UCrafterComponent::HandleStarforceAttempt);
			}

			// 화면에 위젯 추가
			ActiveCraftingUI->AddToViewport();

			// 상태 태그 부여
			if (UAbilitySystemComponent* ASC = Player->GetAbilitySystemComponent())
			{
				ASC->AddLooseGameplayTag(State_Crafting);
			}

			// 입력 모드 변경 (UI와 게임 뷰포트 모두 입력 허용)
			FInputModeGameAndUI InputMode;
			InputMode.SetHideCursorDuringCapture(false);
			InputMode.SetWidgetToFocus(ActiveCraftingUI->TakeWidget()); // 생성한 위젯으로 포커스 이동
			PC->SetInputMode(InputMode);
			PC->bShowMouseCursor = true;

			// WorkTable 전용 IMC 추가 (우선순위 높음)
			if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
			{
				if (WorkTableIMC)
				{
					Subsystem->AddMappingContext(WorkTableIMC, WorkTableIMCPriority);
				}
			}
		}
	}
}

void UCrafterComponent::HandleStarforceAttempt(float ErrorMargin, float BarLength)
{
	if (CurrentInteractingTable.IsValid())
	{
		Server_AttemptCrafting(CurrentInteractingTable.Get(), ErrorMargin, BarLength);
	}
}

void UCrafterComponent::EndCrafting()
{
	UE_LOG(LogTemp, Log, TEXT("UCrafterComponent::EndCrafting called."));
	ABasePlayer* Player = Cast<ABasePlayer>(GetOwner());
	if (!Player) return;

	if (APlayerController* PC = Cast<APlayerController>(Player->GetController()))
	{
		// [로컬/클라] UI 제거 및 입력 모드 복구
		if (PC->IsLocalPlayerController())
		{
			// 화면에 띄워진 UI 제거 및 포인터 초기화
			if (ActiveCraftingUI)
			{
				ActiveCraftingUI->RemoveFromParent();
				ActiveCraftingUI = nullptr;
			}

			// WorkTableIMC 제거
			if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
			{
				if (WorkTableIMC)
				{
					Subsystem->RemoveMappingContext(WorkTableIMC);
				}
			}

			// 입력 모드를 게임 전용으로 복구 및 마우스 커서 비활성화
			FInputModeGameOnly InputMode;
			PC->SetInputMode(InputMode);
			PC->bShowMouseCursor = false;

			// 상태 태그 해제
			if (UAbilitySystemComponent* ASC = Player->GetAbilitySystemComponent())
			{
				ASC->RemoveLooseGameplayTag(State_Crafting);
			}

			// 스타포스 플레이 플래그 초기화
			bIsPlayingStarforce = false;
		}
	}
}

void UCrafterComponent::SpaceBarAction()
{
	if (ABasePlayer* Player = Cast<ABasePlayer>(GetOwner())) {
		if(Player->IsLocallyControlled()) {
			// 띄워진 UI가 StarForceWidget인지 캐스팅하여 확인
			UStarForceWidget* StarForceUI = Cast<UStarForceWidget>(ActiveCraftingUI);
			if (!StarForceUI) return;

			// 상태에 따라 Start / Stop 분기
			if (bIsPlayingStarforce)
			{
				StarForceUI->StopStarForce();
				bIsPlayingStarforce = false;
			}
			else
			{
				StarForceUI->StartStarForce();
				bIsPlayingStarforce = true;
			}
		}
	}
}

void UCrafterComponent::Server_AttemptCrafting_Implementation(AWorkTable* TargetTable, float ErrorMargin, float BarLength)
{
	ABasePlayer* Player = Cast<ABasePlayer>(GetOwner());

	// [서버]에서 스타포스 성공 여부 판정
	if (!Player || !Player->HasAuthority() || !TargetTable) return;

	float AllowedMargin = (BarLength > 0.f) ? (BarLength * 0.5f) : 15.0f;
	bool bIsSuccess = ErrorMargin <= AllowedMargin;
	if (bIsSuccess)
	{
		FGameplayTag CraftedTag = TargetTable->ItemTagToTestCraft;
		if (CraftedTag.IsValid())
		{
			// 제작할 아이템이 재료이면 인벤토리에 재료 추가
			if (CraftedTag.MatchesTag(Item_Material))
			{
				UInventoryComponent* InventoryComponent = Player->GetInventoryComponent();
				if (InventoryComponent)
				{
					InventoryComponent->AddMaterial(CraftedTag, 1);
				}
			}
			//제작할 아이템이 도구이면 생성 로직 시작
			else if (CraftedTag.MatchesTag(Item_Tool))
			{
				bool bHasEmptySlot = Player->HasEmptyItemSlot();
				UWorld* World = GetWorld();
				UItemSubsystem* ItemSubsystem = World ? World->GetSubsystem<UItemSubsystem>() : nullptr;

				if (ItemSubsystem)
				{
					FVector SpawnLocation = TargetTable->GetActorLocation() + FVector(0.f, 0.f, 80.f);
					FTransform SpawnTransform(FRotator::ZeroRotator, SpawnLocation);
					EItemState TargetState = bHasEmptySlot ? EItemState::Dropped_Hovering : EItemState::Dropped_Simulating;

					ABaseItem* SpawnedItem = ItemSubsystem->SpawnItem(CraftedTag, SpawnTransform, TargetState, Player);

					if (SpawnedItem)
					{
						if (bHasEmptySlot)
						{
							Player->TryPutItemInSlot(SpawnedItem);
							UE_LOG(LogTemp, Log, TEXT("CrafterComponent: Successfully crafted and equipped: %s"), *CraftedTag.ToString());
						}
						else
						{
							// 슬롯이 꽉 차서 바닥에 떨어뜨림 
							UE_LOG(LogTemp, Warning, TEXT("CrafterComponent: Quick slots are full! Dropped crafted item: %s"), *CraftedTag.ToString());

							// 아이템의 Root Component를 가져와 물리적 힘(Impulse)을 가합니다.
							if (UPrimitiveComponent* PrimitiveComp = Cast<UPrimitiveComponent>(SpawnedItem->GetRootComponent()))
							{
								// Dropped_Simulating 상태라면 물리 시뮬레이션이 켜져 있을 것이므로 확인 후 실행
								if (PrimitiveComp->IsSimulatingPhysics())
								{
									// Pitch(Y축 회전, 앙각)는 45도 고정, Yaw(Z축 회전)는 0~360도 랜덤
									float RandomYaw = FMath::RandRange(0.f, 360.f);
									FRotator LaunchRotation(45.f, RandomYaw, 0.f);

									// 회전값을 기반으로 날아갈 방향 벡터 추출
									FVector LaunchDirection = LaunchRotation.Vector();

									// 살짝 튀어오를 속도
									constexpr float LaunchSpeed = 400.f;
									FVector LaunchVelocity = LaunchDirection * LaunchSpeed;

									// bVelocityChange를 true로 주어, 아이템의 무게(Mass)를 무시하고 일정한 속도로 튀어오르게 함
									PrimitiveComp->AddImpulse(LaunchVelocity, NAME_None, true);
								}
							}
						}
					}
				}
			}
		}
	}
}