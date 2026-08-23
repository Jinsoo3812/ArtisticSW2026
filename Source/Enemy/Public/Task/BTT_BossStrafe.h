#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "BTT_BossStrafe.generated.h"

class AEnemyShip;
class AShipBossEnemy;

/**
 * Applies a short, deck-local tangential movement input selected once at task
 * start. It performs no clearance query and never fails because movement was
 * blocked, so it is safe before or after an individual boss ability.
 */
UCLASS()
class ENEMY_API UBTT_BossStrafe : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTT_BossStrafe();

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void OnTaskFinished(
		UBehaviorTreeComponent& OwnerComp,
		uint8* NodeMemory,
		EBTNodeResult::Type TaskResult) override;
	virtual FString GetStaticDescription() const override;

	/** Returns the fixed deck-local tangent selected from the initial target radius. */
	static FVector CalculateLocalTangent(
		const FVector& BossLocalLocation,
		const FVector& TargetLocalLocation,
		const FVector& FallbackLocalForward,
		bool bMoveLeft);

	float GetStrafeDuration() const { return StrafeDuration; }
	float GetMoveSpeed() const { return MoveSpeed; }

protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Strafe")
	FBlackboardKeySelector TargetActorKey;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Strafe", meta = (ClampMin = "10.0", Units = "cm/s"))
	float MoveSpeed = 250.0f;

	/** Hard successful duration; blocked movement never extends or fails it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Strafe", meta = (ClampMin = "0.05", ClampMax = "1.0", Units = "s"))
	float StrafeDuration = 0.75f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Strafe")
	bool bRandomizeDirection = true;

	/** Direction used when random selection is disabled. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Strafe", meta = (EditCondition = "!bRandomizeDirection"))
	bool bMoveLeftByDefault = true;

private:
	void StopMovement() const;
	void ResetRuntimeState();

	TWeakObjectPtr<AShipBossEnemy> CachedBoss;
	TWeakObjectPtr<AEnemyShip> CachedHostShip;
	FVector CachedLocalMoveDirection = FVector::ZeroVector;
	float PreviousMaxWalkSpeed = 0.0f;
	float ElapsedMovementTime = 0.0f;
	bool bHasPreviousMaxWalkSpeed = false;
};
