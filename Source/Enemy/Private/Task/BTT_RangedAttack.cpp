#include "Task/BTT_RangedAttack.h"

#include "AIController.h"
#include "AbilitySystemComponent.h"
#include "BaseGameplayTags.h"
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

	AttackExecutionStateTag = State_Attacking;
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

	if (!Enemy || !AbilitySystem || !AttackExecutionStateTag.IsValid() || !Enemy->IsValidCombatTarget(TargetActor))
	{
		return EBTNodeResult::Failed;
	}

	Enemy->SetCombatTarget(TargetActor);
	CachedOwnerComp = &OwnerComp;
	CachedAbilitySystem = AbilitySystem;
	CachedEnemy = Enemy;
	AttackTagDelegateHandle = AbilitySystem->RegisterGameplayTagEvent(AttackExecutionStateTag).AddUObject(
		this,
		&UBTT_RangedAttack::HandleAttackStateTagChanged);

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
	if (!bWaitingForCooldown || bObservedAttackStart)
	{
		return;
	}

	ARangedEnemy* Enemy = CachedEnemy.Get();
	if (!Enemy || !Enemy->CanAttackCurrentTarget(true))
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
	if (bCancelAbilityOnAbort)
	{
		if (UAbilitySystemComponent* AbilitySystem = CachedAbilitySystem.Get())
		{
			FGameplayTagContainer AbilityTags;
			AbilityTags.AddTag(GameplayAbility_RangedAttack);
			AbilitySystem->CancelAbilities(&AbilityTags);
		}
	}

	CleanupTaskState();
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

void UBTT_RangedAttack::HandleAttackStateTagChanged(const FGameplayTag CallbackTag, int32 NewCount)
{
	if (NewCount > 0)
	{
		bObservedAttackStart = true;
		return;
	}

	if (!bObservedAttackStart)
	{
		return;
	}

	if (bActivatingAbility)
	{
		bCompletedSynchronously = true;
		return;
	}

	FinishAttackTask(EBTNodeResult::Succeeded);
}

EBTNodeResult::Type UBTT_RangedAttack::TryActivateCachedAttack()
{
	ARangedEnemy* Enemy = CachedEnemy.Get();
	if (!Enemy || !Enemy->CanAttackCurrentTarget(true))
	{
		return EBTNodeResult::Failed;
	}

	bActivatingAbility = true;
	const bool bActivated = Enemy->TryStartRangedAttack();
	bActivatingAbility = false;

	if (!bActivated)
	{
		return EBTNodeResult::Failed;
	}

	// An attack without a montage can add and remove State.Attacking in the same call.
	if (bCompletedSynchronously)
	{
		return EBTNodeResult::Succeeded;
	}

	return bObservedAttackStart
		? EBTNodeResult::InProgress
		: EBTNodeResult::Failed;
}

void UBTT_RangedAttack::FinishAttackTask(EBTNodeResult::Type Result)
{
	UBehaviorTreeComponent* OwnerComp = CachedOwnerComp.Get();
	CleanupTaskState();
	if (OwnerComp)
	{
		FinishLatentTask(*OwnerComp, Result);
	}
}

void UBTT_RangedAttack::CleanupTaskState()
{
	if (UAbilitySystemComponent* AbilitySystem = CachedAbilitySystem.Get())
	{
		if (AttackTagDelegateHandle.IsValid() && AttackExecutionStateTag.IsValid())
		{
			AbilitySystem->RegisterGameplayTagEvent(AttackExecutionStateTag).Remove(AttackTagDelegateHandle);
		}
	}

	if (ARangedEnemy* Enemy = CachedEnemy.Get())
	{
		Enemy->ClearCombatTarget();
	}

	AttackTagDelegateHandle.Reset();
	CachedOwnerComp.Reset();
	CachedAbilitySystem.Reset();
	CachedEnemy.Reset();
	bObservedAttackStart = false;
	bActivatingAbility = false;
	bCompletedSynchronously = false;
	bWaitingForCooldown = false;
}
