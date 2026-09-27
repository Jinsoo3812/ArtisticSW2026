#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/Decorators/BTDecorator_BlackboardBase.h"

#include "BTD_HasDeckReleaseLOSReposition.generated.h"

/** True while a deck ranged enemy is recovering from a blocked release-frame LOS. */
UCLASS()
class ENEMY_API UBTD_HasDeckReleaseLOSReposition : public UBTDecorator_BlackboardBase
{
	GENERATED_BODY()

public:
	UBTD_HasDeckReleaseLOSReposition();

protected:
	virtual bool CalculateRawConditionValue(
		UBehaviorTreeComponent& OwnerComp,
		uint8* NodeMemory) const override;
};
