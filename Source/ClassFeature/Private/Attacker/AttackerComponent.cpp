// Fill out your copyright notice in the Description page of Project Settings.

#include "AttackerComponent.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputTagConfig.h"
#include "BasePlayer.h"
#include "BaseGameplayTags.h"

UAttackerComponent::UAttackerComponent()
{
	// Component는 굳이 tick이 필요 없으니까
	PrimaryComponentTick.bCanEverTick = false;

	// Component 복제 활성화
	SetIsReplicatedByDefault(true);
}

void UAttackerComponent::BeginPlay()
{
	Super::BeginPlay();

	if (ABasePlayer* Player = Cast<ABasePlayer>(GetOwner()))
	{
		// [서버] Comppnent 초기화 로직 수행
		if (Player->HasAuthority())
		{
			if (Player->GetAbilitySystemComponent())
			{
				// 이미 ASC가 준비되어 있다면 즉시 부여
				GrantAttackerAbilities();
			}
			else
			{
				// 아직 ASC 준비가 안 되었다면, 델리게이트 구독하고 대기
				Player->OnAbilitySystemInitialized.AddUObject(this, &UAttackerComponent::GrantAttackerAbilities);
			}
		}
	}
}

void UAttackerComponent::AddAttackerMappingContext()
{
	ABasePlayer* Player = Cast<ABasePlayer>(GetOwner());
	if (!Player) return;

	if (APlayerController* PC = Cast<APlayerController>(Player->GetController()))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
		{
			if (AttackerIMC)
			{
				Subsystem->AddMappingContext(AttackerIMC, AttackerIMCPriority);
			}
		}
	}
}

void UAttackerComponent::RemoveAttackerMappingContext()
{
	ABasePlayer* Player = Cast<ABasePlayer>(GetOwner());
	if (!Player) return;

	if (APlayerController* PC = Cast<APlayerController>(Player->GetController()))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
		{
			if (AttackerIMC)
			{
				Subsystem->RemoveMappingContext(AttackerIMC);
			}
		}
	}
}

void UAttackerComponent::BindAttackerInput(UEnhancedInputComponent* EnhancedInputComponent)
{
	ABasePlayer* Player = Cast<ABasePlayer>(GetOwner());

	if (!EnhancedInputComponent || !AttackerInputConfig || !Player) return;

	for (const FSlotInputAction& Action : AttackerInputConfig->SlotInputActions)
	{
		if (Action.InputAction && Action.SlotTag.IsValid())
		{
			// BasePlayer의 OnAbilityInputPressed/Released를 활용하여 깔끔하게 태그 기반으로 라우팅
			EnhancedInputComponent->BindAction(Action.InputAction, ETriggerEvent::Started, Player, &ABasePlayer::OnAbilityInputPressed, Action.SlotTag);
			EnhancedInputComponent->BindAction(Action.InputAction, ETriggerEvent::Completed, Player, &ABasePlayer::OnAbilityInputReleased, Action.SlotTag);
		}
	}
}

void UAttackerComponent::GrantAttackerAbilities()
{
	ABasePlayer* Player = Cast<ABasePlayer>(GetOwner());
	if (!Player || !Player->HasAuthority()) return;

	// ASC에 Attacker 역할 태그 부여
	if (UAbilitySystemComponent* ASC = Player->GetAbilitySystemComponent())
	{
		ASC->AddLooseGameplayTag(Class_Attacker);
	}

	for (const auto& SlotGAPair : AttackerAbilities)
	{
		Player->GrantAbilityToSlot(SlotGAPair.Key, SlotGAPair.Value);
	}
}

void UAttackerComponent::RemoveAttackerAbilities()
{
	ABasePlayer* Player = Cast<ABasePlayer>(GetOwner());
	if (!Player || !Player->HasAuthority()) return;

	// ASC에 Attacker 역할 태그 해제
	if (UAbilitySystemComponent* ASC = Player->GetAbilitySystemComponent())
	{
		ASC->RemoveLooseGameplayTag(Class_Attacker);
	}

	for (const auto& SlotGAPair : AttackerAbilities)
	{
		Player->RemoveAbilityFromSlot(SlotGAPair.Key);
	}
}