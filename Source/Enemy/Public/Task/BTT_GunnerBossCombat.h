#pragma once
#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTT_GunnerBossCombat.generated.h"

/** Compact combat subtree entry: positioning, burst priority, single shot, bounded retry. */
UCLASS()
class ENEMY_API UBTT_GunnerBossCombat : public UBTTaskNode
{
	GENERATED_BODY()
public:
	UBTT_GunnerBossCombat();
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void OnTaskFinished(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTNodeResult::Type Result) override;
protected:
	UPROPERTY(EditAnywhere, Category = "Gunner Boss", meta = (ClampMin = "0.01", Units = "s")) float AttackDecisionInterval = 0.15f;
private:
	float DecisionRemaining = 0.f;
};
