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
	ApplyKnockback(TriggerEventData, Direction);
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

	// Reaction is the engine-defined priority for mechanics such as hit reactions.
	// Locking the brain prevents ticking BT tasks (such as Strafe) from issuing a new MoveTo.
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

void UGA_HitReaction::ApplyKnockback(
	const FGameplayEventData& TriggerEventData,
	EBaseHitReactionDirection Direction) const
{
	ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	if (!Character || !Character->HasAuthority()
		|| (KnockbackStrength <= 0.0f && KnockbackUpwardStrength <= 0.0f))
	{
		return;
	}

	UCharacterMovementComponent* MovementComponent = Character->GetCharacterMovement();
	if (!MovementComponent || MovementComponent->MovementMode == MOVE_None)
	{
		return;
	}

	const FVector KnockbackDirection = CalculateKnockbackDirection(TriggerEventData, Direction);
	if (KnockbackDirection.IsNearlyZero())
	{
		return;
	}

	// Guarantee an actual push even when AI movement is currently driving toward the source.
	const float CurrentSpeedAwayFromSource = FVector::DotProduct(MovementComponent->Velocity, KnockbackDirection);
	const float AdditionalSpeedAwayFromSource = FMath::Max(KnockbackStrength - CurrentSpeedAwayFromSource, 0.0f);
	FVector VelocityChange = KnockbackDirection * AdditionalSpeedAwayFromSource;
	VelocityChange.Z = KnockbackUpwardStrength;
	MovementComponent->AddImpulse(VelocityChange, true);
}

FVector UGA_HitReaction::CalculateKnockbackDirection(
	const FGameplayEventData& TriggerEventData,
	EBaseHitReactionDirection Direction) const
{
	const AActor* AvatarActor = GetAvatarActorFromActorInfo();
	if (!AvatarActor)
	{
		return FVector::ZeroVector;
	}

	FVector SourceLocation = FVector::ZeroVector;
	if (TryGetHitReactionSourceLocation(TriggerEventData, SourceLocation))
	{
		FVector AwayFromSource = AvatarActor->GetActorLocation() - SourceLocation;
		AwayFromSource.Z = 0.0f;
		if (AwayFromSource.Normalize())
		{
			return AwayFromSource;
		}
	}

	// If the source actor is unavailable, use the already resolved reaction direction.
	switch (Direction)
	{
	case EBaseHitReactionDirection::Front:
		return -AvatarActor->GetActorForwardVector().GetSafeNormal2D();
	case EBaseHitReactionDirection::Back:
		return AvatarActor->GetActorForwardVector().GetSafeNormal2D();
	case EBaseHitReactionDirection::Left:
		return AvatarActor->GetActorRightVector().GetSafeNormal2D();
	case EBaseHitReactionDirection::Right:
		return -AvatarActor->GetActorRightVector().GetSafeNormal2D();
	default:
		return FVector::ZeroVector;
	}
}
