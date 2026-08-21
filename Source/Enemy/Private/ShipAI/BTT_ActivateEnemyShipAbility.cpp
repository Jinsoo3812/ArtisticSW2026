#include "ShipAI/BTT_ActivateEnemyShipAbility.h"

#include "AIController.h"
#include "Abilities/GameplayAbility.h"
#include "AbilitySystemComponent.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Name.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "ShipAI/EnemyShip.h"
#include "ShipAI/EnemyShipPatternRuntimeComponent.h"

UBTT_ActivateEnemyShipAbility::UBTT_ActivateEnemyShipAbility()
{
	NodeName = TEXT("Activate Enemy Ship Ability");
	bCreateNodeInstance = true;
	bNotifyTaskFinished = true;
	SelectedAbilityTagKey.SelectedKeyName = TEXT("SelectedAbilityTag");
	SelectedAbilityTagKey.AddNameFilter(this, GET_MEMBER_NAME_CHECKED(UBTT_ActivateEnemyShipAbility, SelectedAbilityTagKey));
	SelectedRuleIdKey.SelectedKeyName = TEXT("SelectedAbilityRuleId");
	SelectedRuleIdKey.AddNameFilter(this, GET_MEMBER_NAME_CHECKED(UBTT_ActivateEnemyShipAbility, SelectedRuleIdKey));
}

EBTNodeResult::Type UBTT_ActivateEnemyShipAbility::ExecuteTask(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory)
{
	Cleanup();
	bAborting = false;
	bCompletedSynchronously = false;
	bSynchronousCancelled = false;

	UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();
	AAIController* Controller = OwnerComp.GetAIOwner();
	AEnemyShip* Ship = Controller ? Cast<AEnemyShip>(Controller->GetPawn()) : nullptr;
	UAbilitySystemComponent* ASC = Ship ? Ship->GetAbilitySystemComponent() : nullptr;
	UEnemyShipPatternRuntimeComponent* Runtime = Ship ? Ship->GetPatternRuntimeComponent() : nullptr;
	if (!Blackboard || !ASC || !Runtime || !Ship->HasAuthority())
	{
		return EBTNodeResult::Failed;
	}

	const FName AbilityTagName = Blackboard->GetValueAsName(SelectedAbilityTagKey.SelectedKeyName);
	const FGameplayTag AbilityTag = FGameplayTag::RequestGameplayTag(AbilityTagName, false);
	const FName RuleId = Blackboard->GetValueAsName(SelectedRuleIdKey.SelectedKeyName);
	if (!AbilityTag.IsValid() || RuleId.IsNone())
	{
		return EBTNodeResult::Failed;
	}

	for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
	{
		if (Spec.Ability && Spec.Ability->GetAssetTags().HasTagExact(AbilityTag))
		{
			ActiveHandle = Spec.Handle;
			break;
		}
	}
	if (!ActiveHandle.IsValid())
	{
		return EBTNodeResult::Failed;
	}

	CachedOwnerComp = &OwnerComp;
	CachedASC = ASC;
	AbilityEndedHandle = ASC->OnAbilityEnded.AddUObject(this, &UBTT_ActivateEnemyShipAbility::HandleAbilityEnded);

	bActivating = true;
	const bool bActivated = ASC->TryActivateAbility(ActiveHandle, false);
	bActivating = false;
	if (!bActivated)
	{
		Cleanup();
		return EBTNodeResult::Failed;
	}

	FEnemyShipAbilitySelection Selection;
	Selection.AbilityTag = AbilityTag;
	Selection.RuleId = RuleId;
	if (!Runtime->CommitSelection(Selection))
	{
		ASC->CancelAbilityHandle(ActiveHandle);
		Cleanup();
		return EBTNodeResult::Failed;
	}

	Blackboard->ClearValue(SelectedAbilityTagKey.SelectedKeyName);
	Blackboard->ClearValue(SelectedRuleIdKey.SelectedKeyName);
	if (bCompletedSynchronously)
	{
		const EBTNodeResult::Type Result = bSynchronousCancelled
			? EBTNodeResult::Failed
			: EBTNodeResult::Succeeded;
		Cleanup();
		return Result;
	}
	return EBTNodeResult::InProgress;
}

EBTNodeResult::Type UBTT_ActivateEnemyShipAbility::AbortTask(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory)
{
	bAborting = true;
	if (bCancelAbilityOnAbort)
	{
		if (UAbilitySystemComponent* ASC = CachedASC.Get(); ASC && ActiveHandle.IsValid())
		{
			ASC->CancelAbilityHandle(ActiveHandle);
		}
	}
	Cleanup();
	return EBTNodeResult::Aborted;
}

void UBTT_ActivateEnemyShipAbility::OnTaskFinished(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory,
	EBTNodeResult::Type TaskResult)
{
	Cleanup();
	Super::OnTaskFinished(OwnerComp, NodeMemory, TaskResult);
}

void UBTT_ActivateEnemyShipAbility::HandleAbilityEnded(const FAbilityEndedData& EndedData)
{
	if (EndedData.AbilitySpecHandle != ActiveHandle || bAborting)
	{
		return;
	}
	if (bActivating)
	{
		bCompletedSynchronously = true;
		bSynchronousCancelled = EndedData.bWasCancelled;
		return;
	}
	FinishAbilityTask(EndedData.bWasCancelled ? EBTNodeResult::Failed : EBTNodeResult::Succeeded);
}

void UBTT_ActivateEnemyShipAbility::FinishAbilityTask(EBTNodeResult::Type Result)
{
	UBehaviorTreeComponent* OwnerComp = CachedOwnerComp.Get();
	Cleanup();
	if (OwnerComp)
	{
		FinishLatentTask(*OwnerComp, Result);
	}
}

void UBTT_ActivateEnemyShipAbility::Cleanup()
{
	if (UAbilitySystemComponent* ASC = CachedASC.Get())
	{
		if (AbilityEndedHandle.IsValid())
		{
			ASC->OnAbilityEnded.Remove(AbilityEndedHandle);
		}
	}
	AbilityEndedHandle.Reset();
	CachedOwnerComp.Reset();
	CachedASC.Reset();
	ActiveHandle = FGameplayAbilitySpecHandle();
	bActivating = false;
}
