#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTT_EnableEnemyShipNavigation.generated.h"

UCLASS()
class ENEMY_API UBTT_EnableEnemyShipNavigation : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTT_EnableEnemyShipNavigation();
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;

protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Navigation")
	bool bEnableNavigation = true;
};
