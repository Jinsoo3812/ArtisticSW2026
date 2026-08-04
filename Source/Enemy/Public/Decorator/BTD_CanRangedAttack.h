#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/Decorators/BTDecorator_BlackboardBase.h"

#include "BTD_CanRangedAttack.generated.h"

/** Checks range/target/optional LOS using ARangedEnemy's authoritative combat rules. */
UCLASS()
class ENEMY_API UBTD_CanRangedAttack : public UBTDecorator_BlackboardBase
{
	GENERATED_BODY()

public:
	UBTD_CanRangedAttack();

protected:
	virtual bool CalculateRawConditionValue(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack")
	bool bRequireLineOfSight = true;
};

