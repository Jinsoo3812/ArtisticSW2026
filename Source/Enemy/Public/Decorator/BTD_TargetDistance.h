#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/Decorators/BTDecorator_BlackboardBase.h"

#include "BTD_TargetDistance.generated.h"

UENUM(BlueprintType)
enum class ETargetDistanceQuery : uint8
{
	Within UMETA(DisplayName = "Within Distance"),
	Outside UMETA(DisplayName = "Outside Distance"),
};

/** Periodically observes distance to a moving Blackboard actor. */
UCLASS()
class ENEMY_API UBTD_TargetDistance : public UBTDecorator_BlackboardBase
{
	GENERATED_BODY()

public:
	UBTD_TargetDistance();

	ETargetDistanceQuery GetDistanceQuery() const { return Query; }
	float GetDistanceThreshold() const { return Distance; }

protected:
	virtual bool CalculateRawConditionValue(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const override;
	virtual void OnBecomeRelevant(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual FString GetStaticDescription() const override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Distance")
	ETargetDistanceQuery Query = ETargetDistanceQuery::Outside;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Distance", meta=(ClampMin="0.0", Units="cm"))
	float Distance = 500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Distance")
	bool bUse2DDistance = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Distance", meta=(ClampMin="0.02", Units="s"))
	float EvaluationInterval = 0.1f;
};
