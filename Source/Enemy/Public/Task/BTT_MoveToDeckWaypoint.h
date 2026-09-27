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
	virtual uint16 GetInstanceMemorySize() const override;

	float GetAcceptanceRadius() const { return AcceptanceRadius; }
	float GetMaximumMoveTime() const { return MaximumMoveTime; }
	float GetProgressTimeout() const { return ProgressTimeout; }
	float GetMinimumProgressDistance() const { return MinimumProgressDistance; }

protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Deck AI", meta = (ClampMin = "10.0", Units = "cm/s"))
	float MoveSpeed = 250.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Deck AI", meta = (ClampMin = "10.0", Units = "cm"))
	float AcceptanceRadius = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Deck AI|Failure", meta = (ClampMin = "0.1", Units = "s"))
	float MaximumMoveTime = 6.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Deck AI|Failure", meta = (ClampMin = "0.1", Units = "s"))
	float ProgressTimeout = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Deck AI|Failure", meta = (ClampMin = "1.0", Units = "cm"))
	float MinimumProgressDistance = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Deck AI|Braking", meta = (ClampMin = "0.0", Units = "cm/s^2"))
	float BrakingDeceleration = 1600.0f;
};
