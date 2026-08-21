// Fill out your copyright notice in the Description page of Project Settings.


#include "Abilities/BaseDeathGameplayAbility.h"

#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "BaseGameplayTags.h"
#include "Components/CapsuleComponent.h"
#include "Components/BaseHealthComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "TimerManager.h"

UBaseDeathGameplayAbility::UBaseDeathGameplayAbility()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerInitiated;

	FAbilityTriggerData DeathTrigger;
	DeathTrigger.TriggerSource = EGameplayAbilityTriggerSource::GameplayEvent;
	DeathTrigger.TriggerTag = GameplayAbility_Dead;
	AbilityTriggers.Add(DeathTrigger);

	FGameplayTagContainer DeathAbilityTags;
	DeathAbilityTags.AddTag(GameplayAbility_Dead);
	SetAssetTags(DeathAbilityTags);
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
	bDeathFinished = false;

	FGameplayEventData EmptyEventData;
	const FGameplayEventData& EventData = TriggerEventData ? *TriggerEventData : EmptyEventData;

	ApplyDeathState();

	K2_OnDeathStarted(EventData);

	if (PlayDeathMontage())
	{
		const float PlayRate = FMath::Max(DeathMontagePlayRate, KINDA_SMALL_NUMBER);
		const float CompletionTimeout = DeathMontage
			? (DeathMontage->GetPlayLength() / PlayRate) + DeathCompletionGracePeriod
			: DeathCompletionFallbackTimeout;
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(
				DeathCompletionTimerHandle,
				this,
				&UBaseDeathGameplayAbility::OnDeathCompletionTimeout,
				FMath::Max(CompletionTimeout, 0.1f),
				false);
		}
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("DeathGA: No death montage was played. Avatar=%s Montage=%s"),
		*GetNameSafe(GetAvatarActorFromActorInfo()),
		*GetNameSafe(DeathMontage));

	if (bAutoFinishDeathWithoutMontage)
	{
		FinishDeathWithCancel(false);
	}
}

void UBaseDeathGameplayAbility::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DeathCompletionTimerHandle);
	}

	K2_OnDeathFinished(bWasCancelled);

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UBaseDeathGameplayAbility::OnDeathMontageCompleted()
{
	FinishDeathWithCancel(false);
}

void UBaseDeathGameplayAbility::OnDeathMontageBlendOut()
{
	if (bFinishDeathWhenMontageEnds)
	{
		FinishDeathWithCancel(false);
	}
}

void UBaseDeathGameplayAbility::OnDeathMontageInterrupted()
{
	FinishDeathWithCancel(true);
}

void UBaseDeathGameplayAbility::OnDeathMontageCancelled()
{
	FinishDeathWithCancel(true);
}

void UBaseDeathGameplayAbility::OnDeathCompletionTimeout()
{
	UE_LOG(LogTemp, Error,
		TEXT("DeathGA: Completion timeout reached; forcing FinishDeath. Avatar=%s Montage=%s"),
		*GetNameSafe(GetAvatarActorFromActorInfo()),
		*GetNameSafe(DeathMontage));
	FinishDeathWithCancel(false);
}

UBaseHealthComponent* UBaseDeathGameplayAbility::GetHealthComponentFromAvatar() const
{
	AActor* AvatarActor = GetAvatarActorFromActorInfo();
	return AvatarActor ? AvatarActor->FindComponentByClass<UBaseHealthComponent>() : nullptr;
}

void UBaseDeathGameplayAbility::ApplyDeathState()
{
	ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	if (!Character)
	{
		UE_LOG(LogTemp, Warning, TEXT("DeathGA: Avatar is not a Character. Avatar=%s"),
			*GetNameSafe(GetAvatarActorFromActorInfo()));
		return;
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
}

bool UBaseDeathGameplayAbility::PlayDeathMontage()
{
	if (!DeathMontage)
	{
		return false;
	}

	const ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	const USkeletalMeshComponent* Mesh = Character ? Character->GetMesh() : nullptr;
	UAnimInstance* AnimInstance = Mesh ? Mesh->GetAnimInstance() : nullptr;
	if (!AnimInstance && GetAvatarActorFromActorInfo() && GetAvatarActorFromActorInfo()->GetNetMode() != NM_DedicatedServer)
	{
		UE_LOG(LogTemp, Warning, TEXT("DeathGA: Cannot play death montage because AnimInstance is null. Avatar=%s Mesh=%s Montage=%s"),
			*GetNameSafe(GetAvatarActorFromActorInfo()),
			*GetNameSafe(Mesh),
			*GetNameSafe(DeathMontage));
		return false;
	}

	const float PlayRate = FMath::Max(DeathMontagePlayRate, KINDA_SMALL_NUMBER);

	DeathMontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
		this,
		FName(TEXT("DeathMontageTask")),
		DeathMontage,
		PlayRate,
		DeathMontageStartSection,
		bStopDeathMontageWhenAbilityEnds);

	if (!DeathMontageTask)
	{
		return false;
	}

	DeathMontageTask->OnCompleted.AddDynamic(this, &UBaseDeathGameplayAbility::OnDeathMontageCompleted);
	DeathMontageTask->OnBlendOut.AddDynamic(this, &UBaseDeathGameplayAbility::OnDeathMontageBlendOut);
	DeathMontageTask->OnInterrupted.AddDynamic(this, &UBaseDeathGameplayAbility::OnDeathMontageInterrupted);
	DeathMontageTask->OnCancelled.AddDynamic(this, &UBaseDeathGameplayAbility::OnDeathMontageCancelled);
	DeathMontageTask->ReadyForActivation();

	const bool bIsMontagePlayingNow = AnimInstance ? AnimInstance->Montage_IsPlaying(DeathMontage) : false;

	UE_LOG(LogTemp, Log, TEXT("DeathGA: Playing death montage. Avatar=%s Montage=%s PlayRate=%.3f IsPlayingNow=%d AnimInstance=%s NetMode=%d LocalRole=%d"),
		*GetNameSafe(GetAvatarActorFromActorInfo()),
		*GetNameSafe(DeathMontage),
		PlayRate,
		bIsMontagePlayingNow ? 1 : 0,
		*GetNameSafe(AnimInstance),
		GetAvatarActorFromActorInfo() ? static_cast<int32>(GetAvatarActorFromActorInfo()->GetNetMode()) : INDEX_NONE,
		GetAvatarActorFromActorInfo() ? static_cast<int32>(GetAvatarActorFromActorInfo()->GetLocalRole()) : INDEX_NONE);

	return true;
}

void UBaseDeathGameplayAbility::FinishDeath()
{
	FinishDeathWithCancel(false);
}

void UBaseDeathGameplayAbility::FinishDeathWithCancel(bool bWasCancelled)
{
	if (bDeathFinished)
	{
		return;
	}

	bDeathFinished = true;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DeathCompletionTimerHandle);
	}

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
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, bWasCancelled);
	}
}
