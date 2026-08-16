// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AITypes.h"
#include "Abilities/BaseHitReactionGameplayAbility.h"
#include "GA_HitReaction.generated.h"

class AAIController;

/** Enemy hit reaction that temporarily owns AI movement. */
UCLASS()
class ENEMY_API UGA_HitReaction : public UBaseHitReactionGameplayAbility
{
	GENERATED_BODY()

protected:
	virtual void OnHitReactionActivated(
		const FGameplayEventData& TriggerEventData,
		float DamageAmount,
		EBaseHitReactionDirection Direction) override;

	virtual void EndAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility,
		bool bWasCancelled) override;

private:
	void SuspendAIForHitReaction();
	void RestoreAIAfterHitReaction();

	TWeakObjectPtr<AAIController> SuspendedAIController;
	FAIRequestID SuspendedMoveRequestId;
	bool bLockedAILogicForHitReaction = false;
	bool bPausedAIMoveForHitReaction = false;
};
