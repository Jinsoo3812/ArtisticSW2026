// Fill out your copyright notice in the Description page of Project Settings.


#include "Task/BTT_StartPath.h"
#include "Component/PathMovement.h"
#include "BaseAIController.h"
#include "BaseEnemy.h"

// Unreal
#include "BehaviorTree/BehaviorTreeComponent.h"

UBTT_StartPath::UBTT_StartPath()
{
	NodeName = TEXT("Start Path Movement");
}


EBTNodeResult::Type UBTT_StartPath::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
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

	PathMovement->StartPathMovement();
	return EBTNodeResult::Succeeded;
}
