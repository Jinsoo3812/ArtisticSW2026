#include "Task/BTT_RangedAttack.h"

#include "AIController.h"
#include "AbilitySystemComponent.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "RangedEnemy/RangedEnemy.h"

UBTT_RangedAttack::UBTT_RangedAttack()
{
	NodeName = TEXT("Ranged Attack");
	bCreateNodeInstance = true;
	bNotifyTick = true;
	bNotifyTaskFinished = true;

	BlackboardKey.SelectedKeyName = TEXT("TargetActor");
	BlackboardKey.AddObjectFilter(
		this,
		GET_MEMBER_NAME_CHECKED(UBTT_RangedAttack, BlackboardKey),
		AActor::StaticClass());

}

EBTNodeResult::Type UBTT_RangedAttack::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	CleanupTaskState();

	AAIController* AIController = OwnerComp.GetAIOwner();
	UBlackboardComponent* BlackboardComponent = OwnerComp.GetBlackboardComponent();
	ARangedEnemy* Enemy = AIController ? Cast<ARangedEnemy>(AIController->GetPawn()) : nullptr;
	AActor* TargetActor = BlackboardComponent
		? Cast<AActor>(BlackboardComponent->GetValueAsObject(GetSelectedBlackboardKey()))
		: nullptr;
	UAbilitySystemComponent* AbilitySystem = Enemy ? Enemy->GetAbilitySystemComponent() : nullptr;

	if (!Enemy || !AbilitySystem || !Enemy->IsValidCombatTarget(TargetActor))
	{
		return EBTNodeResult::Failed;
	}

	Enemy->SetCombatTarget(TargetActor);
	CachedOwnerComp = &OwnerComp;
	CachedAbilitySystem = AbilitySystem;
	CachedEnemy = Enemy;
	CachedTarget = TargetActor;

	if (Enemy->GetRemainingAttackCooldown() > KINDA_SMALL_NUMBER)
	{
		bWaitingForCooldown = true;
		return EBTNodeResult::InProgress;
	}

	const EBTNodeResult::Type ActivationResult = TryActivateCachedAttack();
	if (ActivationResult != EBTNodeResult::InProgress)
	{
		CleanupTaskState();
	}
	return ActivationResult;
}

void UBTT_RangedAttack::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	if (!bWaitingForCooldown)
	{
		return;
	}

	ARangedEnemy* Enemy = CachedEnemy.Get();
	if (!Enemy || !CachedTarget.IsValid() || Enemy->GetCombatTarget() != CachedTarget.Get()
		|| !Enemy->CanAttackCurrentTarget(true))
	{
		FinishAttackTask(EBTNodeResult::Failed);
		return;
	}

	if (Enemy->GetRemainingAttackCooldown() > KINDA_SMALL_NUMBER)
	{
		return;
	}

	bWaitingForCooldown = false;
	const EBTNodeResult::Type ActivationResult = TryActivateCachedAttack();
	if (ActivationResult != EBTNodeResult::InProgress)
	{
		FinishAttackTask(ActivationResult);
	}
}

EBTNodeResult::Type UBTT_RangedAttack::AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	bAborting = true;
	UnregisterAbilityEnded();

	if (bCancelAbilityOnAbort && CachedAbilityHandle.IsValid())
	{
		if (UAbilitySystemComponent* AbilitySystem = CachedAbilitySystem.Get())
		{
			AbilitySystem->CancelAbilityHandle(CachedAbilityHandle);
		}
	}

	if (ARangedEnemy* Enemy = CachedEnemy.Get(); Enemy && Enemy->GetCombatTarget() == CachedTarget.Get())
	{
		Enemy->ClearCombatTarget();
	}

	CleanupTaskState(false);
	return EBTNodeResult::Aborted;
}

void UBTT_RangedAttack::OnTaskFinished(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory,
	EBTNodeResult::Type TaskResult)
{
	CleanupTaskState();
	Super::OnTaskFinished(OwnerComp, NodeMemory, TaskResult);
}

void UBTT_RangedAttack::HandleAbilityEnded(const FAbilityEndedData& EndedData)
{
	if (bAborting || bTaskFinished || EndedData.AbilitySpecHandle != CachedAbilityHandle)
	{
		return;
	}

	const EBTNodeResult::Type Result = EndedData.bWasCancelled
		? EBTNodeResult::Failed
		: EBTNodeResult::Succeeded;

	if (bActivatingAbility)
	{
		bCompletedSynchronously = true;
		SynchronousResult = Result;
		return;
	}

	FinishAttackTask(Result);
}

EBTNodeResult::Type UBTT_RangedAttack::TryActivateCachedAttack()
{
	ARangedEnemy* Enemy = CachedEnemy.Get();
	UAbilitySystemComponent* AbilitySystem = CachedAbilitySystem.Get();
	if (!Enemy || !AbilitySystem || !CachedTarget.IsValid()
		|| Enemy->GetCombatTarget() != CachedTarget.Get()
		|| !Enemy->CanAttackCurrentTarget(true))
	{
		return EBTNodeResult::Failed;
	}

	if (!Enemy->FindRangedAttackAbility(CachedAbilityHandle))
	{
		return EBTNodeResult::Failed;
	}

	AbilityEndedDelegateHandle = AbilitySystem->OnAbilityEnded.AddUObject(
		this,
		&UBTT_RangedAttack::HandleAbilityEnded);

	bActivatingAbility = true;
	const bool bActivated = Enemy->TryStartRangedAttack(CachedAbilityHandle);
	bActivatingAbility = false;

	if (!bActivated)
	{
		UnregisterAbilityEnded();
		CachedAbilityHandle = FGameplayAbilitySpecHandle();
		return EBTNodeResult::Failed;
	}

	if (bCompletedSynchronously)
	{
		return SynchronousResult;
	}

	return EBTNodeResult::InProgress;
}

void UBTT_RangedAttack::FinishAttackTask(EBTNodeResult::Type Result)
{
	if (bAborting || bTaskFinished)
	{
		return;
	}

	bTaskFinished = true;
	UBehaviorTreeComponent* OwnerComp = CachedOwnerComp.Get();
	CleanupTaskState(false);
	if (OwnerComp)
	{
		FinishLatentTask(*OwnerComp, Result);
	}
}

void UBTT_RangedAttack::UnregisterAbilityEnded()
{
	if (UAbilitySystemComponent* AbilitySystem = CachedAbilitySystem.Get())
	{
		if (AbilityEndedDelegateHandle.IsValid())
		{
			AbilitySystem->OnAbilityEnded.Remove(AbilityEndedDelegateHandle);
		}
	}
	AbilityEndedDelegateHandle.Reset();
}

void UBTT_RangedAttack::CleanupTaskState(bool bResetLifecycle)
{
	UnregisterAbilityEnded();

	CachedOwnerComp.Reset();
	CachedAbilitySystem.Reset();
	CachedEnemy.Reset();
	CachedTarget.Reset();
	CachedAbilityHandle = FGameplayAbilitySpecHandle();
	SynchronousResult = EBTNodeResult::Failed;
	bActivatingAbility = false;
	bCompletedSynchronously = false;
	bWaitingForCooldown = false;
	if (bResetLifecycle)
	{
		bAborting = false;
		bTaskFinished = false;
	}
}
