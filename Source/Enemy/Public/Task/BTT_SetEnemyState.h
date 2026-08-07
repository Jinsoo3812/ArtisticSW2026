#pragma once

#include "CoreMinimal.h"
#include "AI/EnemyAITypes.h"
#include "BehaviorTree/BTTaskNode.h"

#include "BTT_SetEnemyState.generated.h"

/** Reusable terminal task for state subtrees. */
UCLASS()
class ENEMY_API UBTT_SetEnemyState : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTT_SetEnemyState();

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;

protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "State")
	EEnemyAIState NewState = EEnemyAIState::Passive;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "State")
	bool bClearTargetActor = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "State")
	bool bClearPointOfInterest = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Blackboard", meta = (EditCondition = "bClearPointOfInterest"))
	FName PointOfInterestKeyName = TEXT("PointOfInterest");
};

