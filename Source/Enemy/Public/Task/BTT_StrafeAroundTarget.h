// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/Tasks/BTTask_BlackboardBase.h"
#include "BTT_StrafeAroundTarget.generated.h"

struct FEnemyStrafeTaskMemory
{
	float ElapsedTime;
	float TimeSinceLastMoveRequest;
	int8 DirectionSign;

	void Reset()
	{
		ElapsedTime = 0.0f;
		TimeSinceLastMoveRequest = 0.0f;
		DirectionSign = 1;
	}
};

/**
 * Moves the controlled pawn around the selected target on a circular path.
 *
 * Facing is intentionally not handled here. Use BTS_SetFocusToTarget on the same
 * BT branch so strafe, attack windup, and other combat actions share one focus policy.
 */
UCLASS()
class ENEMY_API UBTT_StrafeAroundTarget : public UBTTask_BlackboardBase
{
	GENERATED_BODY()

public:
	UBTT_StrafeAroundTarget();

protected:
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void OnTaskFinished(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTNodeResult::Type TaskResult) override;
	virtual uint16 GetInstanceMemorySize() const override { return sizeof(FEnemyStrafeTaskMemory); }
	virtual void InitializeMemory(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTMemoryInit::Type InitType) const override;
	virtual void CleanupMemory(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTMemoryClear::Type CleanupType) const override;
	virtual FString GetStaticDescription() const override;

	bool RequestStrafeMove(UBehaviorTreeComponent& OwnerComp, FEnemyStrafeTaskMemory& Memory) const;
	bool CalculateStrafeDestination(UBehaviorTreeComponent& OwnerComp, const FEnemyStrafeTaskMemory& Memory, FVector& OutDestination) const;
	AActor* GetTargetActor(UBehaviorTreeComponent& OwnerComp) const;
	bool ProjectDestinationToNavigation(UBehaviorTreeComponent& OwnerComp, const FVector& DesiredDestination, FVector& OutProjectedDestination) const;

protected:
	UPROPERTY(EditAnywhere, Category = "Strafe", meta = (ClampMin = "0.0", Units = "s"))
	float StrafeDuration = 1.5f;

	UPROPERTY(EditAnywhere, Category = "Strafe", meta = (ClampMin = "0.0", Units = "cm"))
	float DesiredRadius = 450.0f;

	UPROPERTY(EditAnywhere, Category = "Strafe", meta = (ClampMin = "0.0", Units = "deg"))
	float LookAheadAngleDegrees = 25.0f;

	UPROPERTY(EditAnywhere, Category = "Strafe", meta = (ClampMin = "0.01", Units = "s"))
	float MoveRequestInterval = 0.2f;

	UPROPERTY(EditAnywhere, Category = "Strafe", meta = (ClampMin = "0.0", Units = "cm"))
	float AcceptanceRadius = 60.0f;

	UPROPERTY(EditAnywhere, Category = "Strafe")
	bool bClockwise = true;

	UPROPERTY(EditAnywhere, Category = "Strafe")
	bool bRandomizeDirection = false;

	UPROPERTY(EditAnywhere, Category = "Navigation")
	bool bUsePathfinding = true;

	UPROPERTY(EditAnywhere, Category = "Navigation")
	bool bProjectDestinationToNavigation = true;

	UPROPERTY(EditAnywhere, Category = "Navigation")
	bool bAllowPartialPath = true;

	UPROPERTY(EditAnywhere, Category = "Task")
	bool bStopMovementOnFinish = false;
};
