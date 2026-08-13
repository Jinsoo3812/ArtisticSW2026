// Fill out your copyright notice in the Description page of Project Settings.

#include "GAS/Ability/GA_PlayerHitReaction.h"

#include "BasePlayer.h"

void UGA_PlayerHitReaction::OnHitReactionActivated(
	const FGameplayEventData& TriggerEventData,
	float DamageAmount,
	EBaseHitReactionDirection Direction)
{
	Super::OnHitReactionActivated(TriggerEventData, DamageAmount, Direction);

	ABasePlayer* Player = Cast<ABasePlayer>(GetAvatarActorFromActorInfo());
	HitReactingPlayer = Player;
	if (Player)
	{
		Player->bIsHitReacting = true;
	}
}

void UGA_PlayerHitReaction::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	if (ABasePlayer* Player = HitReactingPlayer.Get())
	{
		Player->bIsHitReacting = false;
	}
	HitReactingPlayer.Reset();

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
