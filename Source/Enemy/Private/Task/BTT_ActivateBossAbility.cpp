#include "Task/BTT_ActivateBossAbility.h"

#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbility.h"
#include "AIController.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Int.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BossAI/ShipBossEnemy.h"
#include "Weapon/BaseWeapon.h"
#include "Weapon/BaseWeaponComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogBossAbilityTask, Log, All);

UBTT_ActivateBossAbility::UBTT_ActivateBossAbility()
{
	NodeName = TEXT("Activate Boss Ability");
	bCreateNodeInstance = true;
	DestinationPointKey.SelectedKeyName = TEXT("DestinationPointId");
	DestinationPointKey.AddIntFilter(
		this,
		GET_MEMBER_NAME_CHECKED(UBTT_ActivateBossAbility, DestinationPointKey));
}

EBTNodeResult::Type UBTT_ActivateBossAbility::ExecuteTask(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory)
{
	Cleanup();
	AAIController* Controller = OwnerComp.GetAIOwner();
	AShipBossEnemy* Boss = Controller ? Cast<AShipBossEnemy>(Controller->GetPawn()) : nullptr;
	UAbilitySystemComponent* ASC = Boss ? Boss->GetAbilitySystemComponent() : nullptr;
	if (!ASC || !AbilityAssetTag.IsValid())
	{
		return EBTNodeResult::Failed;
	}

	CachedOwnerComp = &OwnerComp;
	CachedASC = ASC;
	if (bRequirePreselectedDestination && Boss->GetDestinationPointId() == INDEX_NONE)
	{
		ResetDestinationState();
		Cleanup();
		return EBTNodeResult::Failed;
	}

	AbilityEndedDelegateHandle = ASC->OnAbilityEnded.AddUObject(
		this, &UBTT_ActivateBossAbility::HandleAbilityEnded);

	const FGameplayAbilitySpec* Spec = FindAbilitySpec(*Boss, *ASC);
	if (Spec)
	{
		ActiveAbilityHandle = Spec->Handle;
		bExecutingActivation = true;
		const bool bActivated = ASC->TryActivateAbility(Spec->Handle, false);
		bExecutingActivation = false;
		if (!bActivated)
		{
			UE_LOG(LogBossAbilityTask, Warning,
				TEXT("Boss ability activation failed. Boss=%s Tag=%s Ability=%s Source=%s"),
				*GetNameSafe(Boss),
				*AbilityAssetTag.ToString(),
				*GetNameSafe(Spec->Ability),
				*GetNameSafe(Spec->SourceObject.Get()));
			ResetDestinationState();
			Cleanup();
			return EBTNodeResult::Failed;
		}
		if (bEndedDuringActivation)
		{
			const bool bCancelled = bEndedDuringActivationCancelled;
			ResetDestinationState();
			Cleanup();
			return bCancelled ? EBTNodeResult::Failed : EBTNodeResult::Succeeded;
		}
		return EBTNodeResult::InProgress;
	}

	UE_LOG(LogBossAbilityTask, Warning,
		TEXT("No boss ability spec matched. Boss=%s Tag=%s"),
		*GetNameSafe(Boss), *AbilityAssetTag.ToString());
	ResetDestinationState();
	Cleanup();
	return EBTNodeResult::Failed;
}

EBTNodeResult::Type UBTT_ActivateBossAbility::AbortTask(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory)
{
	UAbilitySystemComponent* ASC = CachedASC.Get();
	const FGameplayAbilitySpecHandle AbilityHandle = ActiveAbilityHandle;
	ResetDestinationState();
	Cleanup();
	if (ASC && AbilityHandle.IsValid())
	{
		ASC->CancelAbilityHandle(AbilityHandle);
	}
	return EBTNodeResult::Aborted;
}

FString UBTT_ActivateBossAbility::GetStaticDescription() const
{
	return FString::Printf(
		TEXT("Activate and await %s%s%s"),
		*AbilityAssetTag.ToString(),
		bRequirePreselectedDestination ? TEXT(" (requires destination)") : TEXT(""),
		bPreferCurrentWeaponAbility ? TEXT(" (prefer current weapon spec)") : TEXT(""));
}

void UBTT_ActivateBossAbility::HandleAbilityEnded(const FAbilityEndedData& EndedData)
{
	if (EndedData.AbilitySpecHandle != ActiveAbilityHandle)
	{
		return;
	}
	if (bExecutingActivation)
	{
		bEndedDuringActivation = true;
		bEndedDuringActivationCancelled = EndedData.bWasCancelled;
		return;
	}

	UBehaviorTreeComponent* OwnerComp = CachedOwnerComp.Get();
	const EBTNodeResult::Type Result = EndedData.bWasCancelled
		? EBTNodeResult::Failed
		: EBTNodeResult::Succeeded;
	ResetDestinationState();
	Cleanup();
	if (OwnerComp)
	{
		FinishLatentTask(*OwnerComp, Result);
	}
}

const FGameplayAbilitySpec* UBTT_ActivateBossAbility::FindAbilitySpec(
	const AShipBossEnemy& Boss,
	const UAbilitySystemComponent& AbilitySystem) const
{
	const UObject* CurrentWeapon = nullptr;
	if (const UBaseWeaponComponent* WeaponComponent = Boss.GetWeaponComponent())
	{
		CurrentWeapon = WeaponComponent->GetCurrentWeapon();
	}

	const FGameplayAbilitySpec* FirstMatch = nullptr;
	for (const FGameplayAbilitySpec& Spec : AbilitySystem.GetActivatableAbilities())
	{
		if (!Spec.Ability || !Spec.Ability->GetAssetTags().HasTagExact(AbilityAssetTag))
		{
			continue;
		}

		if (!FirstMatch)
		{
			FirstMatch = &Spec;
		}
		if (bPreferCurrentWeaponAbility && CurrentWeapon && Spec.SourceObject.Get() == CurrentWeapon)
		{
			return &Spec;
		}
	}
	return FirstMatch;
}

void UBTT_ActivateBossAbility::ResetDestinationState()
{
	if (!bClearDestinationWhenFinished)
	{
		return;
	}

	if (UBehaviorTreeComponent* OwnerComp = CachedOwnerComp.Get())
	{
		if (UBlackboardComponent* Blackboard = OwnerComp->GetBlackboardComponent();
			Blackboard && Blackboard->GetKeyID(DestinationPointKey.SelectedKeyName) != FBlackboard::InvalidKey)
		{
			Blackboard->SetValueAsInt(DestinationPointKey.SelectedKeyName, INDEX_NONE);
		}
		if (AAIController* Controller = OwnerComp->GetAIOwner())
		{
			if (AShipBossEnemy* Boss = Cast<AShipBossEnemy>(Controller->GetPawn()))
			{
				Boss->SetDestinationPointId(INDEX_NONE);
			}
		}
	}
}

void UBTT_ActivateBossAbility::Cleanup()
{
	if (UAbilitySystemComponent* ASC = CachedASC.Get(); ASC && AbilityEndedDelegateHandle.IsValid())
	{
		ASC->OnAbilityEnded.Remove(AbilityEndedDelegateHandle);
	}
	CachedOwnerComp.Reset();
	CachedASC.Reset();
	ActiveAbilityHandle = FGameplayAbilitySpecHandle();
	AbilityEndedDelegateHandle.Reset();
	bExecutingActivation = false;
	bEndedDuringActivation = false;
	bEndedDuringActivationCancelled = false;
}
