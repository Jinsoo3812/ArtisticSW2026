// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AITypes.h"
#include "Abilities/BaseHitReactionGameplayAbility.h"
#include "GA_HitReaction.generated.h"

class AAIController;

/**
 * Enemy hit reaction that temporarily owns AI movement and applies a short,
 * server-authoritative knockback away from the hit source.
 */
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

	/** Minimum horizontal speed away from the hit source after the impulse is applied. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "HitReaction|Knockback", meta = (ClampMin = "0.0", Units = "cm/s"))
	float KnockbackStrength = 250.0f;

	/** Optional upward velocity added together with the horizontal knockback. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "HitReaction|Knockback", meta = (ClampMin = "0.0", Units = "cm/s"))
	float KnockbackUpwardStrength = 0.0f;

private:
	void SuspendAIForHitReaction();
	void RestoreAIAfterHitReaction();
	void ApplyKnockback(const FGameplayEventData& TriggerEventData, EBaseHitReactionDirection Direction) const;
	FVector CalculateKnockbackDirection(
		const FGameplayEventData& TriggerEventData,
		EBaseHitReactionDirection Direction) const;

	TWeakObjectPtr<AAIController> SuspendedAIController;
	FAIRequestID SuspendedMoveRequestId;
	bool bLockedAILogicForHitReaction = false;
	bool bPausedAIMoveForHitReaction = false;
};
