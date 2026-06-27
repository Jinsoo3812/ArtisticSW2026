// Fill out your copyright notice in the Description page of Project Settings.


#include "Task/BTT_StartPath.h"
#include "Component/PathMovement.h"
#include "AI/BaseAIController.h"
#include "BaseEnemy.h"

// Unreal
#include "BehaviorTree/BehaviorTreeComponent.h"

UBTT_StartPath::UBTT_StartPath()
{
	NodeName = TEXT("Start Path");
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
	return EBTNodeResult::Succeeded;
}
