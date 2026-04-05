// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTT_EndPath.generated.h"

/**
 * 
 */
UCLASS()
class ENEMY_API UBTT_EndPath : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTT_EndPath();

protected:
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
};
