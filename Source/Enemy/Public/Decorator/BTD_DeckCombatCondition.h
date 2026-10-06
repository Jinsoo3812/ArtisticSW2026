#pragma once
#include "CoreMinimal.h"
#include "BehaviorTree/Decorators/BTDecorator_BlackboardBase.h"
#include "BTD_DeckCombatCondition.generated.h"

UENUM(BlueprintType)
enum class EDeckCombatCondition : uint8 { AttackReady, AttackCooldown, RecoveryPending, AlarmPending };

/** Polls live range/LOS/cooldown; a Blackboard target observer alone cannot detect those changes. */
UCLASS()
class ENEMY_API UBTD_DeckCombatCondition : public UBTDecorator_BlackboardBase
{
	GENERATED_BODY()
public:
	UBTD_DeckCombatCondition();
protected:
	virtual bool CalculateRawConditionValue(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const override;
	virtual void OnBecomeRelevant(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	UPROPERTY(EditAnywhere, Category = "Deck AI")
	EDeckCombatCondition Condition = EDeckCombatCondition::AttackReady;
	UPROPERTY(EditAnywhere, Category = "Deck AI", meta = (ClampMin = "0.02", Units = "s"))
	float CheckInterval = 0.1f;
private:
	float Remaining = 0.0f;
	bool bLastResult = false;
};
