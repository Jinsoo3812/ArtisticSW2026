#pragma once
#include "CoreMinimal.h"
#include "BehaviorTree/Tasks/BTTask_BlackboardBase.h"
#include "BTT_MoveAroundDeckTarget.generated.h"
class ADeckEnemy;

/** Server-side cooldown strafe using supported deck routes, always facing the Player. */
UCLASS()
class ENEMY_API UBTT_MoveAroundDeckTarget : public UBTTask_BlackboardBase
{
	GENERATED_BODY()
public:
	UBTT_MoveAroundDeckTarget();
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void OnTaskFinished(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTNodeResult::Type Result) override;
protected:
	UPROPERTY(EditAnywhere, Category = "Deck AI", meta = (ClampMin = "0.0", Units = "cm"))
	float TargetDistance = 500.0f;
	UPROPERTY(EditAnywhere, Category = "Deck AI", meta = (ClampMin = "1.0", ClampMax = "60.0", Units = "deg"))
	float StrafeAngle = 20.0f;
	UPROPERTY(EditAnywhere, Category = "Deck AI", meta = (ClampMin = "0.0", Units = "cm"))
	float ProjectionTolerance = 100.0f;
	UPROPERTY(EditAnywhere, Category = "Deck AI", meta = (ClampMin = "10.0", Units = "cm/s"))
	float MoveSpeed = 200.0f;
	UPROPERTY(EditAnywhere, Category = "Deck AI", meta = (ClampMin = "0.1", Units = "s"))
	float MaximumDuration = 6.0f;
private:
	bool PlanNextSegment();
	void Cleanup();
	TWeakObjectPtr<ADeckEnemy> Enemy;
	TWeakObjectPtr<AActor> Target;
	float Direction = 1.0f;
	float Elapsed = 0.0f;
	float RetryRemaining = 0.0f;
	float SegmentRemaining = 0.0f;
	bool bOwnsFocus = false;
};
