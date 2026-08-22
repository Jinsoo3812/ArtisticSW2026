#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/Tasks/BTTask_MoveTo.h"

#include "BTT_MoveToWeaponRange.generated.h"

/** Moves to TargetActor using the current weapon's AttackRange as acceptance radius. */
UCLASS()
class ENEMY_API UBTT_MoveToWeaponRange : public UBTTask_MoveTo
{
	GENERATED_BODY()

public:
	UBTT_MoveToWeaponRange(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void OnTaskFinished(
		UBehaviorTreeComponent& OwnerComp,
		uint8* NodeMemory,
		EBTNodeResult::Type TaskResult) override;
	virtual FString GetStaticDescription() const override;

	/** Keeps BT completion safely inside the real weapon range. */
	static float ResolveAcceptanceRange(
		float WeaponAttackRange,
		float Inset,
		float MinimumRange);

	float GetAcceptanceRangeInset() const { return AcceptanceRangeInset; }
	float GetMinimumAcceptanceRange() const { return MinimumAcceptanceRange; }

protected:
	/** Subtracted from AttackRange so the following Basic Attack does not start on the exact boundary. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack Approach", meta = (ClampMin = "0.0", Units = "cm"))
	float AcceptanceRangeInset = 15.0f;

	/** Lower bound used only when the weapon range is large enough to contain it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack Approach", meta = (ClampMin = "0.0", Units = "cm"))
	float MinimumAcceptanceRange = 25.0f;
};
