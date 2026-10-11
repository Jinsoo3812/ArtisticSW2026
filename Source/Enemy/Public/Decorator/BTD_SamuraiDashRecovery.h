#pragma once
#include "CoreMinimal.h"
#include "BehaviorTree/BTDecorator.h"
#include "BTD_SamuraiDashRecovery.generated.h"

UENUM()
enum class ESamuraiDashRecoveryCondition : uint8 { RepositionReady, AllowDash, AllowNormalBehavior };

/** Routes Samurai policy; does not modify destinations or cancel abilities. */
UCLASS()
class ENEMY_API UBTD_SamuraiDashRecovery : public UBTDecorator
{
	GENERATED_BODY()
public:
	UBTD_SamuraiDashRecovery();
	void Configure(ESamuraiDashRecoveryCondition InCondition);
	ESamuraiDashRecoveryCondition GetCondition() const { return Condition; }
	virtual FString GetStaticDescription() const override;
protected:
	virtual bool CalculateRawConditionValue(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const override;
	virtual void OnBecomeRelevant(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	UPROPERTY(EditAnywhere, Category = "Samurai|Dash Recovery")
	ESamuraiDashRecoveryCondition Condition = ESamuraiDashRecoveryCondition::AllowDash;
};
