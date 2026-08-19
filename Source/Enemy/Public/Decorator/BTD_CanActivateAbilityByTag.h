#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTDecorator.h"
#include "GameplayTagContainer.h"
#include "BTD_CanActivateAbilityByTag.generated.h"

/**
 * Advisory BT-side ability selection. The Gameplay Ability remains the final
 * authority for cooldown, cost, and activation-tag validation.
 */
UCLASS()
class ENEMY_API UBTD_CanActivateAbilityByTag : public UBTDecorator
{
	GENERATED_BODY()

public:
	UBTD_CanActivateAbilityByTag();
	FGameplayTag GetAbilityAssetTag() const { return AbilityAssetTag; }

protected:
	virtual bool CalculateRawConditionValue(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const override;
	virtual void OnBecomeRelevant(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual FString GetStaticDescription() const override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability")
	FGameplayTag AbilityAssetTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability", meta = (ClampMin = "0.02", Units = "s"))
	float EvaluationInterval = 0.1f;
};
