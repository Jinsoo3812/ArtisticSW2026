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
				GrantCrafterAbilities();
			}
			else
			{
				// 아직 셋업되지 않았다면 델리게이트 바인딩 (Dedicated Server 대응)
				Player->OnAbilitySystemInitialized.AddUObject(this, &UCrafterComponent::GrantCrafterAbilities);
			}
		}
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

	for (const FKeyInputAction& Action : CrafterInputConfig->KeyInputActions)
	{
		if (Action.InputAction && Action.KeyTag.IsValid())
		{
			// Crafter의 IA와 KeyTag 매핑을 Player에게 적용
			// IMC 우선순위로 인해 동일 키입력에 대해 우선 적용
			EnhancedInputComponent->BindAction(Action.InputAction, ETriggerEvent::Started, Player, &ABasePlayer::OnAbilityInputPressed, Action.KeyTag);
			EnhancedInputComponent->BindAction(Action.InputAction, ETriggerEvent::Completed, Player, &ABasePlayer::OnAbilityInputReleased, Action.KeyTag);
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

