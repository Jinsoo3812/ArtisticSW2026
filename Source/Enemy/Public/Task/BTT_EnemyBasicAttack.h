// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTT_EnemyBasicAttack.generated.h"


UCLASS()
class ENEMY_API UBTT_EnemyBasicAttack : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTT_EnemyBasicAttack();

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
};
