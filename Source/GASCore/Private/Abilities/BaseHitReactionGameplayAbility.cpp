// Fill out your copyright notice in the Description page of Project Settings.


#include "Abilities/BaseHitReactionGameplayAbility.h"

#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "BaseGameplayTags.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "Components/SkeletalMeshComponent.h"

UBaseHitReactionGameplayAbility::UBaseHitReactionGameplayAbility()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerInitiated;
	bRetriggerInstancedAbility = true;

	FGameplayTagContainer HitReactionTags;
	HitReactionTags.AddTag(GameplayAbility_HitReaction);
	SetAssetTags(HitReactionTags);

	CancelAbilitiesWithTag.AddTag(GameplayAbility_InterruptibleByHit);
	BlockAbilitiesWithTag.AddTag(GameplayAbility_InterruptibleByHit);
	ActivationOwnedTags.AddTag(State_Damaged);
	ActivationBlockedTags.AddTag(State_Dead);

	FAbilityTriggerData HitReactionTrigger;
	HitReactionTrigger.TriggerSource = EGameplayAbilityTriggerSource::GameplayEvent;
	HitReactionTrigger.TriggerTag = GameplayAbility_HitReaction;
	AbilityTriggers.Add(HitReactionTrigger);
}

void UBaseHitReactionGameplayAbility::ActivateAbility(
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

	FGameplayEventData EmptyEventData;
	const FGameplayEventData& EventData = TriggerEventData ? *TriggerEventData : EmptyEventData;

	bHitReactionFinished = false;
	CurrentHitReactionDirection = CalculateHitReactionDirection(EventData);

	OnHitReactionActivated(EventData, EventData.EventMagnitude, CurrentHitReactionDirection);
	K2_OnHitReactionStarted(EventData, EventData.EventMagnitude);
	K2_OnDirectionalHitReactionStarted(EventData, EventData.EventMagnitude, CurrentHitReactionDirection);

	if (PlayHitReactionMontage(CurrentHitReactionDirection))
	{
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("HitReactionGA: No hit reaction montage was played. Avatar=%s Direction=%d"),
		*GetNameSafe(GetAvatarActorFromActorInfo()),
		static_cast<int32>(CurrentHitReactionDirection));

	if (bAutoFinishHitReactionWithoutMontage)
	{
		FinishHitReaction(false);
	}
}

void UBaseHitReactionGameplayAbility::OnHitReactionMontageCompleted()
{
	FinishHitReaction(false);
}

void UBaseHitReactionGameplayAbility::OnHitReactionMontageInterrupted()
{
	FinishHitReaction(true);
}

void UBaseHitReactionGameplayAbility::OnHitReactionMontageCancelled()
{
	FinishHitReaction(true);
}

bool UBaseHitReactionGameplayAbility::PlayHitReactionMontage(EBaseHitReactionDirection Direction)
{
	UAnimMontage* HitReactionMontage = GetHitReactionMontage(Direction);
	if (!HitReactionMontage)
	{
		return false;
	}

	const ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	const USkeletalMeshComponent* Mesh = Character ? Character->GetMesh() : nullptr;
	UAnimInstance* AnimInstance = Mesh ? Mesh->GetAnimInstance() : nullptr;
	if (!AnimInstance && GetAvatarActorFromActorInfo() && GetAvatarActorFromActorInfo()->GetNetMode() != NM_DedicatedServer)
	{
		UE_LOG(LogTemp, Warning, TEXT("HitReactionGA: Cannot play montage because AnimInstance is null. Avatar=%s Mesh=%s Montage=%s"),
			*GetNameSafe(GetAvatarActorFromActorInfo()),
			*GetNameSafe(Mesh),
			*GetNameSafe(HitReactionMontage));
		return false;
	}

	HitReactionMontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
		this,
		FName(TEXT("HitReactionMontageTask")),
		HitReactionMontage,
		FMath::Max(HitReactionMontagePlayRate, KINDA_SMALL_NUMBER),
		HitReactionMontageStartSection,
		bStopHitReactionMontageWhenAbilityEnds,
		0.0f);

	if (!HitReactionMontageTask)
	{
		return false;
	}

	HitReactionMontageTask->OnCompleted.AddDynamic(this, &UBaseHitReactionGameplayAbility::OnHitReactionMontageCompleted);
	HitReactionMontageTask->OnInterrupted.AddDynamic(this, &UBaseHitReactionGameplayAbility::OnHitReactionMontageInterrupted);
	HitReactionMontageTask->OnCancelled.AddDynamic(this, &UBaseHitReactionGameplayAbility::OnHitReactionMontageCancelled);
	HitReactionMontageTask->ReadyForActivation();

	return true;
}

EBaseHitReactionDirection UBaseHitReactionGameplayAbility::CalculateHitReactionDirection(const FGameplayEventData& TriggerEventData) const
{
	const AActor* AvatarActor = GetAvatarActorFromActorInfo();
	if (!AvatarActor)
	{
		return DefaultHitReactionDirection;
	}

	FVector SourceLocation = FVector::ZeroVector;
	if (!TryGetHitReactionSourceLocation(TriggerEventData, SourceLocation))
	{
		UE_LOG(LogTemp, Warning, TEXT("HitReactionGA: Falling back to default direction because source location was not found. Avatar=%s Instigator=%s EffectCauser=%s SourceObject=%s OptionalObject=%s"),
			*GetNameSafe(AvatarActor),
			*GetNameSafe(TriggerEventData.Instigator.Get()),
			*GetNameSafe(TriggerEventData.ContextHandle.GetEffectCauser()),
			*GetNameSafe(TriggerEventData.ContextHandle.GetSourceObject()),
			*GetNameSafe(TriggerEventData.OptionalObject.Get()));
		return DefaultHitReactionDirection;
	}

	FVector ToSource = SourceLocation - AvatarActor->GetActorLocation();
	ToSource.Z = 0.0f;
	if (!ToSource.Normalize())
	{
		return DefaultHitReactionDirection;
	}

	FVector Forward = AvatarActor->GetActorForwardVector();
	Forward.Z = 0.0f;
	Forward.Normalize();

	FVector Right = AvatarActor->GetActorRightVector();
	Right.Z = 0.0f;
	Right.Normalize();

	const float ForwardDot = FVector::DotProduct(Forward, ToSource);
	const float RightDot = FVector::DotProduct(Right, ToSource);

	if (FMath::Abs(ForwardDot) >= FMath::Abs(RightDot))
	{
		return ForwardDot >= 0.0f ? EBaseHitReactionDirection::Front : EBaseHitReactionDirection::Back;
	}

	return RightDot >= 0.0f ? EBaseHitReactionDirection::Right : EBaseHitReactionDirection::Left;
}

void UBaseHitReactionGameplayAbility::OnHitReactionActivated(
	const FGameplayEventData& TriggerEventData,
	float DamageAmount,
	EBaseHitReactionDirection Direction)
{
}

UAnimMontage* UBaseHitReactionGameplayAbility::GetHitReactionMontage(EBaseHitReactionDirection Direction) const
{
	switch (Direction)
	{
	case EBaseHitReactionDirection::Front:
		return FrontHitReactionMontage;
	case EBaseHitReactionDirection::Back:
		return BackHitReactionMontage;
	case EBaseHitReactionDirection::Left:
		return LeftHitReactionMontage;
	case EBaseHitReactionDirection::Right:
		return RightHitReactionMontage;
	default:
		return nullptr;
	}
}

void UBaseHitReactionGameplayAbility::FinishHitReaction(bool bWasCancelled)
{
	if (bHitReactionFinished)
	{
		return;
	}

	bHitReactionFinished = true;
	K2_OnHitReactionFinished(bWasCancelled);

	if (IsActive())
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, bWasCancelled);
	}
}

bool UBaseHitReactionGameplayAbility::TryGetHitReactionSourceLocation(const FGameplayEventData& TriggerEventData, FVector& OutSourceLocation) const
{
	const AActor* AvatarActor = GetAvatarActorFromActorInfo();

	auto TryUseActor = [&OutSourceLocation, AvatarActor](const AActor* SourceActor)
	{
		if (!SourceActor || SourceActor == AvatarActor)
		{
			return false;
		}

		OutSourceLocation = SourceActor->GetActorLocation();
		return true;
	};

	if (TryUseActor(TriggerEventData.Instigator.Get()))
	{
		return true;
	}

	if (TryUseActor(TriggerEventData.ContextHandle.GetOriginalInstigator()))
	{
		return true;
	}

	if (TryUseActor(TriggerEventData.ContextHandle.GetEffectCauser()))
	{
		return true;
	}

	if (const AActor* SourceObjectActor = Cast<AActor>(TriggerEventData.ContextHandle.GetSourceObject()))
	{
		if (TryUseActor(SourceObjectActor))
		{
			return true;
		}
	}

	if (const AActor* OptionalActor = Cast<AActor>(TriggerEventData.OptionalObject.Get()))
	{
		if (TryUseActor(OptionalActor))
		{
			return true;
		}
	}

	if (const FHitResult* HitResult = TriggerEventData.ContextHandle.GetHitResult())
	{
		OutSourceLocation = HitResult->ImpactPoint;
		return true;
	}

	return false;
}
