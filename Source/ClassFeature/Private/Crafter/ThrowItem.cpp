// Fill out your copyright notice in the Description page of Project Settings.


#include "ThrowItem.h"
#include "BasePlayer.h"
#include "BaseGameplayTags.h"

void UThrowItem::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	ABasePlayer* Player = Cast<ABasePlayer>(ActorInfo->AvatarActor.Get());
	if (Player)
	{
		Player->ThrowEquippedItem();
		EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
	}
	else
	{
		// 예외 처리: 플레이어를 캐스팅할 수 없는 경우 취소(Cancel) 처리로 종료
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
	}
}

void UThrowItem::EndAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility, bool bWasCancelled)
{

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}