#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"

#include "BTT_EquipEnemyWeapon.generated.h"

/** Equips the already spawned default weapon. Safe to execute repeatedly. */
UCLASS()
class ENEMY_API UBTT_EquipEnemyWeapon : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTT_EquipEnemyWeapon();

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
};
