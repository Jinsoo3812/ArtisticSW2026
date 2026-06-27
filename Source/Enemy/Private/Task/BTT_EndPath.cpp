// Fill out your copyright notice in the Description page of Project Settings.


#include "Task/BTT_EndPath.h"
#include "AI/BaseAIController.h"
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
	
	return EBTNodeResult::Succeeded;
}
