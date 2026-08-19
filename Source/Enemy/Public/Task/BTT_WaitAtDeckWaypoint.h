#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTT_WaitAtDeckWaypoint.generated.h"

struct FWaitAtDeckWaypointMemory
{
	float RemainingTime = 0.0f;
};

/** Uses the current waypoint's authored random wait range. */
UCLASS()
class ENEMY_API UBTT_WaitAtDeckWaypoint : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTT_WaitAtDeckWaypoint();

	virtual uint16 GetInstanceMemorySize() const override;
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
};
