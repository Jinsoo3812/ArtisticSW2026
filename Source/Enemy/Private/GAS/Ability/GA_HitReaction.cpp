// Fill out your copyright notice in the Description page of Project Settings.

#include "GAS/Ability/GA_HitReaction.h"

#include "AIController.h"
#include "BrainComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

void UGA_HitReaction::OnHitReactionActivated(
	const FGameplayEventData& TriggerEventData,
	float DamageAmount,
	EBaseHitReactionDirection Direction)
{
	Super::OnHitReactionActivated(TriggerEventData, DamageAmount, Direction);
	SuspendAIForHitReaction();
}

void UGA_HitReaction::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	RestoreAIAfterHitReaction();

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UGA_HitReaction::SuspendAIForHitReaction()
{
	const ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	AAIController* AIController = Character ? Cast<AAIController>(Character->GetController()) : nullptr;
	if (!AIController || !Character->HasAuthority())
	{
		return;
	}

	SuspendedAIController = AIController;

	// Stop the velocity already produced by path following without changing MovementMode.
	// The paused path can therefore resume after the reaction without forcing Walking.
	if (UCharacterMovementComponent* MovementComponent = Character->GetCharacterMovement())
	{
		MovementComponent->StopMovementImmediately();
	}

	// Reaction is the engine-defined priority for mechanics such as hit reactions.
	// Locking the brain prevents active BT tasks from issuing a new MoveTo.
	if (UBrainComponent* BrainComponent = AIController->GetBrainComponent())
	{
		BrainComponent->LockResource(EAIRequestPriority::Reaction);
		bLockedAILogicForHitReaction = true;
	}

	// Keep the active path request instead of aborting it. Aborting would make BT MoveTo tasks
	// finish as failed, while pausing lets path following continue from the displaced location.
	SuspendedMoveRequestId = AIController->GetCurrentMoveRequestID();
	if (SuspendedMoveRequestId.IsValid())
	{
		bPausedAIMoveForHitReaction = AIController->PauseMove(SuspendedMoveRequestId);
	}
}

void UGA_HitReaction::RestoreAIAfterHitReaction()
{
	AAIController* AIController = SuspendedAIController.Get();
	if (AIController)
	{
		// Restore the paused path before allowing the BT to tick and issue movement requests again.
		if (bPausedAIMoveForHitReaction && SuspendedMoveRequestId.IsValid())
		{
			AIController->ResumeMove(SuspendedMoveRequestId);
		}

		if (bLockedAILogicForHitReaction)
		{
			if (UBrainComponent* BrainComponent = AIController->GetBrainComponent())
			{
				BrainComponent->ClearResourceLock(EAIRequestPriority::Reaction);
			}
		}
	}

	SuspendedAIController.Reset();
	SuspendedMoveRequestId = FAIRequestID();
	bLockedAILogicForHitReaction = false;
	bPausedAIMoveForHitReaction = false;
}
