#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTT_WaitAtDeckWaypoint.generated.h"

struct FWaitAtDeckWaypointMemory
{
	float RemainingTime = 0.0f;
};

/** Patrol cadence belongs to the task, independent of spawn anchors. */
UCLASS()
class ENEMY_API UBTT_WaitAtDeckWaypoint : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTT_WaitAtDeckWaypoint();

	virtual uint16 GetInstanceMemorySize() const override;
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;

protected:
	UPROPERTY(EditAnywhere, Category = "Deck AI|Patrol", meta = (ClampMin = "0.0", Units = "s"))
	float MinWaitTime = 0.5f;
	UPROPERTY(EditAnywhere, Category = "Deck AI|Patrol", meta = (ClampMin = "0.0", Units = "s"))
	float MaxWaitTime = 2.0f;
};
