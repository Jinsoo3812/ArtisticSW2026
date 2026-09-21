#include "Abilities/StunGameplayAbility.h"
#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "AIController.h"
#include "BrainComponent.h"
#include "BaseGameplayTags.h"
#include "Components/StatusComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

UStunGameplayAbility::UStunGameplayAbility()
{
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerInitiated;
	bAllowDuringControlBlock = true;
	ActivationBlockedTags.Reset();
	ActivationBlockedTags.AddTag(State_Dead);
	ActivationRequiredTags.AddTag(State_Status_Stun);
	SetAssetTags(FGameplayTagContainer(GameplayAbility_Status_Stun));
}

void UStunGameplayAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	if (!ASC || !Character)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}
	// Only GE expiration/removal ends this reaction; ordinary hits cannot shorten it.
	SetCanBeCanceled(false);
	StunDelegate = ASC->RegisterGameplayTagEvent(State_Status_Stun).AddUObject(this, &UStunGameplayAbility::HandleStunChanged);
	if (Character->HasAuthority())
	{
		TArray<FGameplayAbilitySpecHandle> ToCancel;
		for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
		{
			const UBaseGameplayAbility* Ability = Cast<UBaseGameplayAbility>(Spec.Ability);
			if (Spec.IsActive() && Spec.Handle != Handle && (!Ability || !Ability->IsAllowedDuringControlBlock()))
				ToCancel.Add(Spec.Handle);
		}
		for (FGameplayAbilitySpecHandle ActiveHandle : ToCancel) ASC->CancelAbilityHandle(ActiveHandle);
		// Cancel previous reactions first, so their cleanup cannot release our AI lock.
		if (AAIController* AI = Cast<AAIController>(Character->GetController()))
		{
			LockedAI = AI;
			if (UBrainComponent* Brain = AI->GetBrainComponent()) Brain->LockResource(EAIRequestPriority::Reaction);
			AI->StopMovement();
		}
	}
	Character->StopJumping();
	Character->ConsumeMovementInputVector();
	if (UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
	{
		LockedMovement = Movement;
		bRestoreOrientToMovement = Movement->bOrientRotationToMovement;
		bRestoreDesiredRotation = Movement->bUseControllerDesiredRotation;
		Movement->bOrientRotationToMovement = false;
		Movement->bUseControllerDesiredRotation = false;
		Movement->StopMovementImmediately();
	}
	if (const UStatusComponent* Status = Character->FindComponentByClass<UStatusComponent>(); Status && Status->StunMontage)
	{
		auto* Task = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
			this, TEXT("StunMontage"), Status->StunMontage);
		if (Task) Task->ReadyForActivation();
	}
}

void UStunGameplayAbility::HandleStunChanged(FGameplayTag Tag, int32 Count)
{
	if (Count == 0 && IsActive()) EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

void UStunGameplayAbility::EndAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility, bool bWasCancelled)
{
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	const bool bDead = ASC && ASC->HasMatchingGameplayTag(State_Dead);
	if (ASC) ASC->RegisterGameplayTagEvent(State_Status_Stun).Remove(StunDelegate);
	if (UCharacterMovementComponent* Movement = LockedMovement.Get(); Movement && !bDead)
	{
		Movement->bOrientRotationToMovement = bRestoreOrientToMovement;
		Movement->bUseControllerDesiredRotation = bRestoreDesiredRotation;
	}
	if (AAIController* AI = LockedAI.Get())
	{
		if (UBrainComponent* Brain = AI->GetBrainComponent())
		{
			Brain->ClearResourceLock(EAIRequestPriority::Reaction);
			if (bDead) Brain->StopLogic(TEXT("Death overrides Stun recovery"));
		}
	}
	LockedAI.Reset();
	LockedMovement.Reset();
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
