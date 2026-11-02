#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTT_SummonDeckEnemy.generated.h"

/** Requests one validated pooled deck enemy from the boss's host ship. */
UCLASS()
class ENEMY_API UBTT_SummonDeckEnemy : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTT_SummonDeckEnemy();
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	UPROPERTY(EditAnywhere, Category="Boss|Summon") bool bWaitForBossIdle = false;
private:
	EBTNodeResult::Type TryExecute(UBehaviorTreeComponent& OwnerComp);
};
