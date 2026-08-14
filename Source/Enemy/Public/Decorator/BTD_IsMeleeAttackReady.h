#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/Decorators/BTDecorator_BlackboardBase.h"
#include "GameplayTagContainer.h"

#include "BTD_IsMeleeAttackReady.generated.h"

class UAbilitySystemComponent;
class UBehaviorTreeComponent;

/**
 * Selects the melee attack branch while the weapon attack is off cooldown.
 * Distance is intentionally handled by BTT_MoveToWeaponRange inside that branch.
 */
UCLASS()
class ENEMY_API UBTD_IsMeleeAttackReady : public UBTDecorator_BlackboardBase
{
	GENERATED_BODY()

public:
	UBTD_IsMeleeAttackReady();

	FGameplayTag GetObservedCooldownTag() const { return CooldownTag; }

protected:
	virtual bool CalculateRawConditionValue(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const override;
	virtual void OnBecomeRelevant(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void OnCeaseRelevant(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual FString GetStaticDescription() const override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack")
	FGameplayTag CooldownTag;

private:
	void HandleCooldownTagChanged(FGameplayTag ChangedTag, int32 NewCount);
	void UnbindCooldownObserver();

	TWeakObjectPtr<UBehaviorTreeComponent> CachedOwnerComp;
	TWeakObjectPtr<UAbilitySystemComponent> CachedAbilitySystem;
	FDelegateHandle CooldownTagDelegateHandle;
};
