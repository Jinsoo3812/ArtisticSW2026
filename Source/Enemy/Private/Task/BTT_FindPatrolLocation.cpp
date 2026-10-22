// Fill out your copyright notice in the Description page of Project Settings.

#include "Task/BTT_FindPatrolLocation.h"

// Unreal
#include "AIController.h"
#include "AI/PointSelectionFailure.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Float.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Vector.h"
#include "NavigationSystem.h"

UBTT_FindPatrolLocation::UBTT_FindPatrolLocation()
{
	NodeName = TEXT("Find Patrol Location");

	HomeLocationKey.SelectedKeyName = TEXT("HomeLocation");
	HomeLocationKey.AddVectorFilter(this, GET_MEMBER_NAME_CHECKED(UBTT_FindPatrolLocation, HomeLocationKey));

	PatrolLocationKey.SelectedKeyName = TEXT("PatrolLocation");
	PatrolLocationKey.AddVectorFilter(this, GET_MEMBER_NAME_CHECKED(UBTT_FindPatrolLocation, PatrolLocationKey));

	PatrolRadiusKey.SelectedKeyName = TEXT("PatrolRadius");
	PatrolRadiusKey.AddFloatFilter(this, GET_MEMBER_NAME_CHECKED(UBTT_FindPatrolLocation, PatrolRadiusKey));
}

EBTNodeResult::Type UBTT_FindPatrolLocation::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	AAIController* AIController = OwnerComp.GetAIOwner();
	UBlackboardComponent* BlackboardComponent = OwnerComp.GetBlackboardComponent();
	if (!AIController || !BlackboardComponent)
	{
		return EBTNodeResult::Failed;
	}

	const APawn* ControlledPawn = AIController->GetPawn();
	if (!ControlledPawn)
	{
		return EBTNodeResult::Failed;
	}

	const FVector HomeLocation = BlackboardComponent->GetValueAsVector(HomeLocationKey.SelectedKeyName);
	float PatrolRadius = BlackboardComponent->GetValueAsFloat(PatrolRadiusKey.SelectedKeyName);
	if (PatrolRadius <= 0.0f)
	{
		PatrolRadius = DefaultPatrolRadius;
	}

	UNavigationSystemV1* NavigationSystem = UNavigationSystemV1::GetCurrent(ControlledPawn->GetWorld());
	if (!NavigationSystem)
	{
		EnemyPointSelectionFailure::Log(this, ControlledPawn, TEXT("Navigation is unavailable for patrol point selection."));
		BlackboardComponent->ClearValue(PatrolLocationKey.SelectedKeyName);
		return EBTNodeResult::Failed;
	}

	FNavLocation PatrolNavLocation;
	const bool bFoundLocation = NavigationSystem->GetRandomReachablePointInRadius(
		HomeLocation,
		PatrolRadius,
		PatrolNavLocation
	);

	if (!bFoundLocation)
	{
		EnemyPointSelectionFailure::Log(this, ControlledPawn, TEXT("No reachable patrol point in the requested radius."));
		BlackboardComponent->ClearValue(PatrolLocationKey.SelectedKeyName);
		return EBTNodeResult::Failed;
	}

	BlackboardComponent->SetValueAsVector(PatrolLocationKey.SelectedKeyName, PatrolNavLocation.Location);
	return EBTNodeResult::Succeeded;
}
