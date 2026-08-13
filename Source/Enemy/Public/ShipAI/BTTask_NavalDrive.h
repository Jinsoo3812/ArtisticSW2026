#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_NavalDrive.generated.h"

/**
 * Compatibility node for the existing BT_NavalAI asset.
 * Persistent steering now belongs to UEnemyShipNavigationComponent; this task
 * only enables it and completes immediately. Replace the asset node with
 * BTT_EnableEnemyShipNavigation during content migration.
 */
UCLASS(meta = (
	DisplayName = "[LEGACY] Naval Drive Compatibility",
	DeprecatedNode,
	DeprecationMessage = "LEGACY compatibility only. Replace with BTT_EnableEnemyShipNavigation and delete this class."))
class ENEMY_API UBTTask_NavalDrive : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_NavalDrive();
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
};
