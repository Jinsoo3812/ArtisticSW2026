#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTT_MoveToDeckWaypoint.generated.h"

/** Follows the waypoint's live ship-relative transform instead of a cached world-space goal. */
UCLASS()
class ENEMY_API UBTT_MoveToDeckWaypoint : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTT_MoveToDeckWaypoint();

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;

protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Deck AI", meta = (ClampMin = "10.0", Units = "cm/s"))
	float MoveSpeed = 250.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Deck AI", meta = (ClampMin = "10.0", Units = "cm"))
	float AcceptanceRadius = 80.0f;
};
