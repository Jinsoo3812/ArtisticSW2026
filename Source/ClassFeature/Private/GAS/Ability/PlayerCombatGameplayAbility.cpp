// Fill out your copyright notice in the Description page of Project Settings.

#include "GAS/Ability/PlayerCombatGameplayAbility.h"

#include "AbilitySystemComponent.h"
#include "BaseGameplayTags.h"
#include "BasePlayer.h"

UPlayerCombatGameplayAbility::UPlayerCombatGameplayAbility()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;

	ActivationBlockedTags.AddTag(State_Swimming);
	ActivationBlockedTags.AddTag(State_Rolling);
	ActivationBlockedTags.AddTag(State_Damaged);
	ActivationBlockedTags.AddTag(State_Dead);
}

bool UPlayerCombatGameplayAbility::CanActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags,
	FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
	{
		return false;
	}

	if (ActorInfo && ActorInfo->AbilitySystemComponent.IsValid())
	{
		const UAbilitySystemComponent* ASC = ActorInfo->AbilitySystemComponent.Get();
		if (ASC->HasMatchingGameplayTag(State_Swimming)
			|| ASC->HasMatchingGameplayTag(State_Rolling)
			|| ASC->HasMatchingGameplayTag(State_Damaged)
			|| ASC->HasMatchingGameplayTag(State_Dead))
		{
			return false;
		}
	}

	const AActor* Avatar = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
	if (const ABasePlayer* Player = Cast<ABasePlayer>(Avatar))
	{
		if (!Player->CanPerformCombatAction())
		{
			return false;
		}
	}

	return true;
}
