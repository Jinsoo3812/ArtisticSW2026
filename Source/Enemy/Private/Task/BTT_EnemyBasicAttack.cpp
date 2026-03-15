// Fill out your copyright notice in the Description page of Project Settings.


#include "Public/task/BTT_EnemyBasicAttack.h"

UBTT_EnemyBasicAttack::UBTT_EnemyBasicAttack()
{
	NodeName = TEXT("Enemy Basic Attack");
	
}

EBTNodeResult::Type UBTT_EnemyBasicAttack::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	return Super::ExecuteTask(OwnerComp, NodeMemory);
}
