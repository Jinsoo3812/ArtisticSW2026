// Fill out your copyright notice in the Description page of Project Settings.


#include "Abilities/BaseDeathGameplayAbility.h"

#include "BaseGameplayTags.h"
#include "Components/CapsuleComponent.h"
#include "Components/BaseHealthComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

UBaseDeathGameplayAbility::UBaseDeathGameplayAbility()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;

	FAbilityTriggerData DeathTrigger;
	DeathTrigger.TriggerSource = EGameplayAbilityTriggerSource::GameplayEvent;
	DeathTrigger.TriggerTag = GameplayAbility_Dead;
	AbilityTriggers.Add(DeathTrigger);
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

	if (bAutoApplyDeathRagdoll)
	{
		ApplyDeathRagdoll();
	}

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

void UBaseDeathGameplayAbility::ApplyDeathRagdoll()
{
	if (bDeathRagdollApplied)
	{
		return;
	}

	ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	if (!Character)
	{
		return;
	}

	if (bDetachFromController)
	{
		Character->DetachFromControllerPendingDestroy();
	}

	if (bDisableMovement)
	{
		if (UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
		{
			Movement->DisableMovement();
			Movement->StopMovementImmediately();
		}
	}

	if (bDisableCapsuleCollision)
	{
		if (UCapsuleComponent* Capsule = Character->GetCapsuleComponent())
		{
			Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
	}

	USkeletalMeshComponent* Mesh = Character->GetMesh();
	if (!Mesh)
	{
		return;
	}

	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Mesh->SetSimulatePhysics(true);

	FVector Impulse = Character->GetActorForwardVector() * -DeathBackwardImpulse;
	Impulse.Z = DeathUpwardImpulse;
	Mesh->AddImpulseAtLocation(Impulse, Character->GetActorLocation());

	bDeathRagdollApplied = true;
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
