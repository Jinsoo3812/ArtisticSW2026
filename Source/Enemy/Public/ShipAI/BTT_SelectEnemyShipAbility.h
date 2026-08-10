#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "BTT_SelectEnemyShipAbility.generated.h"

UCLASS()
class ENEMY_API UBTT_SelectEnemyShipAbility : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTT_SelectEnemyShipAbility();
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	FName GetTargetShipKeyName() const { return TargetShipKey.SelectedKeyName; }
	FName GetSelectedAbilityTagKeyName() const { return SelectedAbilityTagKey.SelectedKeyName; }
	FName GetSelectedRuleIdKeyName() const { return SelectedRuleIdKey.SelectedKeyName; }

protected:
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector TargetShipKey;

	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector SelectedAbilityTagKey;

	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector SelectedRuleIdKey;
};
