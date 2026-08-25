// Fill out your copyright notice in the Description page of Project Settings.

#include "GAS/Ability/GA_PlayerHitReaction.h"

#include "Abilities/Tasks/AbilityTask_ApplyRootMotionConstantForce.h"
#include "Animation/AnimMontage.h"
#include "BasePlayer.h"
#include "SWCharacterMovementComponent.h"

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

		if (USWCharacterMovementComponent* Movement =
			Cast<USWCharacterMovementComponent>(Player->GetCharacterMovement()))
		{
			const FVector RootMotionDirection = ResolveRootMotionDirection(TriggerEventData, Direction);
			Movement->BeginHitReactionRootMotion(RootMotionDirection);
			HitReactionMovementComponent = Movement;
			StartFallbackRootMotion(Direction, RootMotionDirection);
		}
	}
}

FVector UGA_PlayerHitReaction::ResolveRootMotionDirection(
	const FGameplayEventData& TriggerEventData,
	EBaseHitReactionDirection Direction) const
{
	const AActor* Avatar = GetAvatarActorFromActorInfo();
	if (!Avatar)
	{
		return FVector::ZeroVector;
	}

	FVector SourceLocation = FVector::ZeroVector;
	if (TryGetHitReactionSourceLocation(TriggerEventData, SourceLocation))
	{
		FVector AwayFromSource = Avatar->GetActorLocation() - SourceLocation;
		AwayFromSource.Z = 0.0f;
		if (AwayFromSource.Normalize())
		{
			return AwayFromSource;
		}
	}

	switch (Direction)
	{
	case EBaseHitReactionDirection::Front:
		return -Avatar->GetActorForwardVector();
	case EBaseHitReactionDirection::Back:
		return Avatar->GetActorForwardVector();
	case EBaseHitReactionDirection::Left:
		return Avatar->GetActorRightVector();
	case EBaseHitReactionDirection::Right:
		return -Avatar->GetActorRightVector();
	default:
		return -Avatar->GetActorForwardVector();
	}
}

void UGA_PlayerHitReaction::StartFallbackRootMotion(
	EBaseHitReactionDirection Direction,
	const FVector& WorldDirection)
{
	const UAnimMontage* Montage = GetHitReactionMontage(Direction);
	if ((Montage && Montage->HasRootMotion())
		|| WorldDirection.IsNearlyZero()
		|| FallbackRootMotionStrength <= 0.0f
		|| FallbackRootMotionDuration <= 0.0f)
	{
		return;
	}

	FallbackRootMotionTask = UAbilityTask_ApplyRootMotionConstantForce::ApplyRootMotionConstantForce(
		this,
		FName(TEXT("HitReactionFallbackRootMotion")),
		WorldDirection,
		FallbackRootMotionStrength,
		FallbackRootMotionDuration,
		false,
		nullptr,
		ERootMotionFinishVelocityMode::ClampVelocity,
		FVector::ZeroVector,
		120.0f,
		true);
	if (FallbackRootMotionTask)
	{
		FallbackRootMotionTask->ReadyForActivation();
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
	if (USWCharacterMovementComponent* Movement = HitReactionMovementComponent.Get())
	{
		Movement->EndHitReactionRootMotion();
	}
	HitReactionMovementComponent.Reset();
	FallbackRootMotionTask = nullptr;
	HitReactingPlayer.Reset();

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
