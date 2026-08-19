#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/Decorators/BTDecorator_BlackboardBase.h"

#include "BTD_CombatTargetState.generated.h"

UENUM()
enum class ECombatTargetStateQuery : uint8
{
	IsSet,
	IsNotSet,
};

UCLASS()
class ENEMY_API UBTD_CombatTargetState : public UBTDecorator_BlackboardBase
{
	GENERATED_BODY()

public:
	UBTD_CombatTargetState();

	ECombatTargetStateQuery GetQuery() const { return Query; }

protected:
	virtual bool CalculateRawConditionValue(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const override;
	virtual FString GetStaticDescription() const override;

	UPROPERTY(EditAnywhere, Category = "Target")
	ECombatTargetStateQuery Query = ECombatTargetStateQuery::IsSet;
};
