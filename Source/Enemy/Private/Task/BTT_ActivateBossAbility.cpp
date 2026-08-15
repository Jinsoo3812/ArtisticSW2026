#include "Task/BTT_ActivateBossAbility.h"

#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbility.h"
#include "AIController.h"
#include "BossAI/ShipBossEnemy.h"

UBTT_ActivateBossAbility::UBTT_ActivateBossAbility()
{
	NodeName = TEXT("Activate Boss Ability");
	bCreateNodeInstance = true;
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
	AbilityEndedDelegateHandle = ASC->OnAbilityEnded.AddUObject(
		this, &UBTT_ActivateBossAbility::HandleAbilityEnded);

	for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
	{
		if (!Spec.Ability || !Spec.Ability->GetAssetTags().HasTagExact(AbilityAssetTag))
		{
			continue;
		}

		ActiveAbilityHandle = Spec.Handle;
		bExecutingActivation = true;
		const bool bActivated = ASC->TryActivateAbility(Spec.Handle, false);
		bExecutingActivation = false;
		if (!bActivated)
		{
			Cleanup();
			return EBTNodeResult::Failed;
		}
		if (bEndedDuringActivation)
		{
			const bool bCancelled = bEndedDuringActivationCancelled;
			Cleanup();
			return bCancelled ? EBTNodeResult::Failed : EBTNodeResult::Succeeded;
		}
		return EBTNodeResult::InProgress;
	}

	Cleanup();
	return EBTNodeResult::Failed;
}

EBTNodeResult::Type UBTT_ActivateBossAbility::AbortTask(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory)
{
	if (UAbilitySystemComponent* ASC = CachedASC.Get(); ASC && ActiveAbilityHandle.IsValid())
	{
		ASC->CancelAbilityHandle(ActiveAbilityHandle);
	}
	Cleanup();
	return EBTNodeResult::Aborted;
}

FString UBTT_ActivateBossAbility::GetStaticDescription() const
{
	return FString::Printf(TEXT("Activate and await %s"), *AbilityAssetTag.ToString());
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
	Cleanup();
	if (OwnerComp)
	{
		FinishLatentTask(*OwnerComp, Result);
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
