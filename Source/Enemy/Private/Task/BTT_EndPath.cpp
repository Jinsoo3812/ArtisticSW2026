// Fill out your copyright notice in the Description page of Project Settings.


#include "Task/BTT_EndPath.h"
#include "BaseAIController.h"
#include "BaseEnemy.h"
#include "Component/PathMovement.h"

// Unreal
#include "BehaviorTree/BehaviorTreeComponent.h"

UBTT_EndPath::UBTT_EndPath()
{
	NodeName = TEXT("End Path");
}

EBTNodeResult::Type UBTT_EndPath::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	AAIController* AIController = OwnerComp.GetAIOwner();
	if (!AIController)
	{
		return EBTNodeResult::Failed;
	}

	ABaseEnemy* Enemy = Cast<ABaseEnemy>(AIController->GetPawn());
	if (!Enemy)
	{
		return EBTNodeResult::Failed;
	}

	UPathMovement* PathMovement = Enemy->GetPathMovementComponent();
	if (!PathMovement || !PathMovement->GetCurrentPath() || PathMovement->HasReachedGoal())
	{
		return EBTNodeResult::Failed;
	}

	PathMovement->StopPathMovement();
	return EBTNodeResult::Succeeded;
}
