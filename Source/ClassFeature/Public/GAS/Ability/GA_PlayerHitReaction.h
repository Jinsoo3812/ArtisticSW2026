// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/BaseHitReactionGameplayAbility.h"
#include "GA_PlayerHitReaction.generated.h"

class ABasePlayer;
class UAbilityTask_ApplyRootMotionConstantForce;
class USWCharacterMovementComponent;

/**
 * Player-specific hit reaction.
 *
 * The base class owns ability interruption, retriggering, and montage playback.
 * This subclass deliberately does not stop CharacterMovement, so normal movement
 * remains available while the player-specific hit-reaction state is active.
 */
UCLASS()
class CLASSFEATURE_API UGA_PlayerHitReaction : public UBaseHitReactionGameplayAbility
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
	FVector ResolveRootMotionDirection(
		const FGameplayEventData& TriggerEventData,
		EBaseHitReactionDirection Direction) const;
	void StartFallbackRootMotion(
		EBaseHitReactionDirection Direction,
		const FVector& WorldDirection);

	UPROPERTY(EditDefaultsOnly, Category = "HitReaction|Root Motion", meta = (ClampMin = "0.0", Units = "cm/s"))
	float FallbackRootMotionStrength = 450.0f;

	UPROPERTY(EditDefaultsOnly, Category = "HitReaction|Root Motion", meta = (ClampMin = "0.0", Units = "s"))
	float FallbackRootMotionDuration = 0.22f;

	TWeakObjectPtr<ABasePlayer> HitReactingPlayer;
	TWeakObjectPtr<USWCharacterMovementComponent> HitReactionMovementComponent;

	UPROPERTY(Transient)
	TObjectPtr<UAbilityTask_ApplyRootMotionConstantForce> FallbackRootMotionTask;
};
