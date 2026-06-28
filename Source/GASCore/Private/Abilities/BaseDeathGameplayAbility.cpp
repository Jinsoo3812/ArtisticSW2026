// Fill out your copyright notice in the Description page of Project Settings.


#include "Abilities/BaseDeathGameplayAbility.h"

#include "Components/BaseHealthComponent.h"
#include "GameFramework/Actor.h"

UBaseDeathGameplayAbility::UBaseDeathGameplayAbility()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
}

void UBaseDeathGameplayAbility::ActivateAbility(
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

	CachedHealthComponent = GetHealthComponentFromAvatar();

	FGameplayEventData EmptyEventData;
	const FGameplayEventData& EventData = TriggerEventData ? *TriggerEventData : EmptyEventData;
	K2_OnDeathStarted(EventData);
}

void UBaseDeathGameplayAbility::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	K2_OnDeathFinished(bWasCancelled);

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

UBaseHealthComponent* UBaseDeathGameplayAbility::GetHealthComponentFromAvatar() const
{
	AActor* AvatarActor = GetAvatarActorFromActorInfo();
	return AvatarActor ? AvatarActor->FindComponentByClass<UBaseHealthComponent>() : nullptr;
}

void UBaseDeathGameplayAbility::FinishDeath()
{
	if (!CachedHealthComponent)
	{
		CachedHealthComponent = GetHealthComponentFromAvatar();
	}

	if (CachedHealthComponent)
	{
		CachedHealthComponent->FinishDeath();
	}

	if (IsActive())
	{
		K2_EndAbility();
	}
}
