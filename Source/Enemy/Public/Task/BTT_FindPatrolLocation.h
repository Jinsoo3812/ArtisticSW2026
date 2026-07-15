// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "BTT_FindPatrolLocation.generated.h"

UCLASS()
class ENEMY_API UBTT_FindPatrolLocation : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTT_FindPatrolLocation();

protected:
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector HomeLocationKey;

	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector PatrolLocationKey;

	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector PatrolRadiusKey;

	UPROPERTY(EditAnywhere, Category = "Patrol", meta = (ClampMin = "0.0"))
	float DefaultPatrolRadius = 800.0f;

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
};
