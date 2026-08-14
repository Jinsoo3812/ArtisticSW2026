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
	virtual FString GetStaticDescription() const override;
};
