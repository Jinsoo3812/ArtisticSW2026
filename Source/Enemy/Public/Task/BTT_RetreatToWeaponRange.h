#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/Tasks/BTTask_BlackboardBase.h"

#include "BTT_RetreatToWeaponRange.generated.h"

class AAIController;
class ARangedEnemy;

/** Moves away from a close target until the equipped weapon's usable range is restored. */
UCLASS()
class ENEMY_API UBTT_RetreatToWeaponRange : public UBTTask_BlackboardBase
{
	GENERATED_BODY()

public:
	UBTT_RetreatToWeaponRange();

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void OnTaskFinished(
		UBehaviorTreeComponent& OwnerComp,
		uint8* NodeMemory,
		EBTNodeResult::Type TaskResult) override;
	virtual FString GetStaticDescription() const override;

	static float ResolveDesiredRange(float WeaponAttackRange, float RangeInset, float MinimumDesiredRange);
	static FVector ResolvePlanarAwayDirection(
		const FVector& EnemyLocation,
		const FVector& TargetLocation,
		const FVector& TargetForward,
		const FVector& EnemyForward);

	float GetRangeInset() const { return RangeInset; }
	float GetMinimumDesiredRange() const { return MinimumDesiredRange; }
	float GetAcceptanceRadius() const { return AcceptanceRadius; }
	float GetRepathInterval() const { return RepathInterval; }
	float GetMaximumMoveTime() const { return MaximumMoveTime; }

protected:
	bool ResolveContext(
		UBehaviorTreeComponent& OwnerComp,
		ARangedEnemy*& OutEnemy,
		AActor*& OutTarget,
		float& OutWeaponRange,
		float& OutDesiredRange) const;
	bool FindBestRetreatDestination(
		const ARangedEnemy& Enemy,
		const AActor& Target,
		float WeaponRange,
		float DesiredRange,
		FVector& OutDestination) const;
	bool RequestMove(AAIController& Controller, const FVector& Destination);
	void StopMovement(UBehaviorTreeComponent& OwnerComp) const;
	void ResetRuntimeState();

	/** Keeps the destination safely inside the exact weapon-range boundary. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Retreat", meta = (ClampMin = "0.0", Units = "cm"))
	float RangeInset = 75.0f;

	/** Must remain greater than the close-range trigger so the retreat cannot finish immediately. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Retreat", meta = (ClampMin = "0.0", Units = "cm"))
	float MinimumDesiredRange = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Retreat", meta = (ClampMin = "1.0", Units = "cm"))
	float AcceptanceRadius = 50.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Retreat|Navigation", meta = (ClampMin = "1.0"))
	FVector NavigationProjectionExtent = FVector(150.0f, 150.0f, 250.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Retreat|Navigation", meta = (ClampMin = "0.05", Units = "s"))
	float RepathInterval = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Retreat|Navigation", meta = (ClampMin = "1.0", Units = "cm"))
	float TargetMovementRepathThreshold = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Retreat|Failure", meta = (ClampMin = "0.1", Units = "s"))
	float MaximumMoveTime = 6.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Retreat|Failure", meta = (ClampMin = "0.1", Units = "s"))
	float ProgressTimeout = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Retreat|Failure", meta = (ClampMin = "1.0", Units = "cm"))
	float MinimumProgressDistance = 20.0f;

private:
	TWeakObjectPtr<ARangedEnemy> ActiveEnemy;
	TWeakObjectPtr<AActor> ActiveTarget;
	FVector LastTargetLocation = FVector::ZeroVector;
	float ElapsedTime = 0.0f;
	float TimeSinceProgress = 0.0f;
	float TimeSinceRepath = 0.0f;
	float ProgressAnchorDistance = 0.0f;
};
