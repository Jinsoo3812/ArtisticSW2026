// Fill out your copyright notice in the Description page of Project Settings.


#include "Abilities/BaseHitReactionGameplayAbility.h"

UBaseHitReactionGameplayAbility::UBaseHitReactionGameplayAbility()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerInitiated;
}

void UBaseHitReactionGameplayAbility::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	FGameplayEventData EmptyEventData;
	const FGameplayEventData& EventData = TriggerEventData ? *TriggerEventData : EmptyEventData;

	K2_OnHitReactionStarted(EventData, EventData.EventMagnitude);
}
