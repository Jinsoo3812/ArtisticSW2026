#include "Task/BTT_SetEnemyState.h"

#include "AI/BaseAIController.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"

UBTT_SetEnemyState::UBTT_SetEnemyState()
{
	NodeName = TEXT("Set Enemy State");
}

EBTNodeResult::Type UBTT_SetEnemyState::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	ABaseAIController* AIController = Cast<ABaseAIController>(OwnerComp.GetAIOwner());
	if (!AIController)
	{
		return EBTNodeResult::Failed;
	}

	if (bClearTargetActor)
	{
		AIController->ClearCombatTarget(false);
	}

	if (bClearPointOfInterest)
	{
		if (UBlackboardComponent* BlackboardComponent = OwnerComp.GetBlackboardComponent())
		{
			BlackboardComponent->ClearValue(PointOfInterestKeyName);
		}
	}

	return AIController->SetEnemyState(NewState)
		? EBTNodeResult::Succeeded
		: EBTNodeResult::Failed;
}

